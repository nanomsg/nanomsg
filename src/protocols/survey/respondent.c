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

#include "respondent.h"
#include "xrespondent.h"

#include "../../sp.h"
#include "../../survey.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/wire.h"

#include <stdint.h>
#include <string.h>

#define SP_RESPONDENT_INPROGRESS 1

struct sp_respondent {
    struct sp_xrespondent xrespondent;
    uint32_t surveyid;
    uint32_t flags;
};

/*  Private functions. */
static void sp_respondent_init (struct sp_respondent *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_respondent_term (struct sp_respondent *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_respondent_destroy (struct sp_sockbase *self);
static int sp_respondent_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_respondent_recv (struct sp_sockbase *self, void *buf,
    size_t *len);
static const struct sp_sockbase_vfptr sp_respondent_sockbase_vfptr = {
    sp_respondent_destroy,
    sp_xrespondent_add,
    sp_xrespondent_rm,
    sp_xrespondent_in,
    sp_xrespondent_out,
    sp_respondent_send,
    sp_respondent_recv,
    sp_xrespondent_setopt,
    sp_xrespondent_getopt
};

static void sp_respondent_init (struct sp_respondent *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_xrespondent_init (&self->xrespondent, vfptr, fd);
    self->flags = 0;
}

static void sp_respondent_term (struct sp_respondent *self)
{
    sp_xrespondent_term (&self->xrespondent);
}

void sp_respondent_destroy (struct sp_sockbase *self)
{
    struct sp_respondent *respondent;

    respondent = sp_cont (self, struct sp_respondent, xrespondent.sockbase);

    sp_respondent_term (respondent);
    sp_free (respondent);
}

static int sp_respondent_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_respondent *respondent;

    respondent = sp_cont (self, struct sp_respondent, xrespondent.sockbase);

    /*  If there's no survey going on, report EFSM error. */
    if (sp_slow (!(respondent->flags & SP_RESPONDENT_INPROGRESS)))
        return -EFSM;

    /*  Tag the message with survey ID. */
    sp_assert (sp_chunkref_size (&msg->hdr) == 0);
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_init (&msg->hdr, 4);
    sp_putl (sp_chunkref_data (&msg->hdr), respondent->surveyid);

    /*  Try to send the message. If it cannot be sent due to pushback, drop it
        silently. */
    rc = sp_xrespondent_send (&respondent->xrespondent.sockbase, msg);
    if (sp_slow (rc == -EAGAIN)) {
        sp_msg_term (msg);
        return -EAGAIN;
    }
    errnum_assert (rc == 0, -rc);

    /*  Remember that no survey is being processed. */
    respondent->flags &= ~SP_RESPONDENT_INPROGRESS;

    return 0;
}

static int sp_respondent_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_respondent *respondent;
    size_t tmplen;
    uint8_t *tmpbuf;

    respondent = sp_cont (self, struct sp_respondent, xrespondent.sockbase);

    /*  Cancel current survey, if it exists. */
    respondent->flags &= ~SP_RESPONDENT_INPROGRESS;

    /*  Get next survey. Split it into survey ID and the body. */
    tmplen = *len + 4;
    tmpbuf = sp_alloc (tmplen, "survey");
    alloc_assert (tmpbuf);
    rc = sp_xrespondent_recv (&respondent->xrespondent.sockbase,
        tmpbuf, &tmplen);
    if (sp_slow (rc == -EAGAIN)) {
        sp_free (tmpbuf);
        return -EAGAIN;
    }
    errnum_assert (rc == 0, -rc);
    respondent->surveyid = sp_getl (tmpbuf);
    memcpy (buf, tmpbuf + 4, tmplen - 4);
    *len = tmplen - 4;
    sp_free (tmpbuf);

    /*  Remember that survey is being processed. */
    respondent->flags |= SP_RESPONDENT_INPROGRESS;

    return 0;
}

static struct sp_sockbase *sp_respondent_create (int fd)
{
    struct sp_respondent *self;

    self = sp_alloc (sizeof (struct sp_respondent), "socket (respondent)");
    alloc_assert (self);
    sp_respondent_init (self, &sp_respondent_sockbase_vfptr, fd);
    return &self->xrespondent.sockbase;
}

static struct sp_socktype sp_respondent_socktype_struct = {
    AF_SP,
    SP_RESPONDENT,
    sp_respondent_create
};

struct sp_socktype *sp_respondent_socktype = &sp_respondent_socktype_struct;

