/*
    Copyright (c) 2012-2013 250bpm s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "rep.h"
#include "xrep.h"

#include "../../sp.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/chunkref.h"
#include "../../utils/wire.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SP_REP_INPROGRESS 1

struct sp_rep {
    struct sp_xrep xrep;
    uint32_t flags;
    struct sp_chunkref backtrace;
};

/*  Private functions. */
static void sp_rep_init (struct sp_rep *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_rep_term (struct sp_rep *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_rep_destroy (struct sp_sockbase *self);
static int sp_rep_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_rep_recv (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_rep_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen);
static int sp_rep_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen);

static const struct sp_sockbase_vfptr sp_rep_sockbase_vfptr = {
    sp_rep_destroy,
    sp_xrep_add,
    sp_xrep_rm,
    sp_xrep_in,
    sp_xrep_out,
    sp_rep_send,
    sp_rep_recv,
    sp_xrep_setopt,
    sp_xrep_getopt,
    sp_rep_sethdr,
    sp_rep_gethdr
};

static void sp_rep_init (struct sp_rep *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_xrep_init (&self->xrep, vfptr, fd);
    self->flags = 0;
}

static void sp_rep_term (struct sp_rep *self)
{
    if (self->flags & SP_REP_INPROGRESS)
        sp_chunkref_term (&self->backtrace);
    sp_xrep_term (&self->xrep);
}

static void sp_rep_destroy (struct sp_sockbase *self)
{
    struct sp_rep *rep;

    rep = sp_cont (self, struct sp_rep, xrep.sockbase);

    sp_rep_term (rep);
    sp_free (rep);
}

static int sp_rep_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_rep *rep;
    size_t replylen;
    uint8_t *reply;

    rep = sp_cont (self, struct sp_rep, xrep.sockbase);

    /*  If no request was received, there's nowhere to send the reply to. */
    if (sp_slow (!(rep->flags & SP_REP_INPROGRESS)))
        return -EFSM;

    /*  Move the stored backtrace into the message header. */
    sp_assert (sp_chunkref_size (&msg->hdr) == 0);
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_mv (&msg->hdr, &rep->backtrace);
    rep->flags &= ~SP_REP_INPROGRESS;

    /*  Send the reply. If it cannot be sent because of pushback,
        drop it silently. */
    rc = sp_xrep_send (&rep->xrep.sockbase, msg);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);

    return 0;
}

static int sp_rep_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_rep *rep;

    rep = sp_cont (self, struct sp_rep, xrep.sockbase);

    /*  If a request is already being processed, cancel it. */
    if (sp_slow (rep->flags & SP_REP_INPROGRESS)) {
        sp_chunkref_term (&rep->backtrace);
        rep->flags &= ~SP_REP_INPROGRESS;
    }

    /*  Receive the request. */
    rc = sp_xrep_recv (&rep->xrep.sockbase, msg);
    if (sp_slow (rc == -EAGAIN))
        return -EAGAIN;
    errnum_assert (rc == 0, -rc);

    /*  Store the backtrace. */
    sp_chunkref_mv (&rep->backtrace, &msg->hdr);
    sp_chunkref_init (&msg->hdr, 0);
    rep->flags |= SP_REP_INPROGRESS;

    return 0;
}

static int sp_rep_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (sp_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int sp_rep_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

static struct sp_sockbase *sp_rep_create (int fd)
{
    struct sp_rep *self;

    self = sp_alloc (sizeof (struct sp_rep), "socket (rep)");
    alloc_assert (self);
    sp_rep_init (self, &sp_rep_sockbase_vfptr, fd);
    return &self->xrep.sockbase;
}

static struct sp_socktype sp_rep_socktype_struct = {
    AF_SP,
    SP_REP,
    sp_rep_create
};

struct sp_socktype *sp_rep_socktype = &sp_rep_socktype_struct;

