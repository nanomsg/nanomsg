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

#include "req.h"
#include "xreq.h"

#include "../../sp.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SP_REQ_DEFAULT_RESEND_IVL 60000

#define SP_REQ_INPROGRESS 1

struct sp_req {
    struct sp_xreq xreq;
    const struct sp_cp_sink *sink;
    uint32_t reqid;
    uint32_t flags;
    struct sp_msg request;
    int resend_ivl;
    struct sp_timer resend_timer;
};

/*  Private functions. */
static void sp_req_init (struct sp_req *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_req_term (struct sp_req *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_req_destroy (struct sp_sockbase *self);
static int sp_req_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_req_recv (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_req_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_req_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static int sp_req_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen);
static int sp_req_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen);
static const struct sp_sockbase_vfptr sp_req_sockbase_vfptr = {
    sp_req_destroy,
    sp_xreq_add,
    sp_xreq_rm,
    sp_xreq_in,
    sp_xreq_out,
    sp_req_send,
    sp_req_recv,
    sp_req_setopt,
    sp_req_getopt,
    sp_req_sethdr,
    sp_req_gethdr
};

/*  Event sink. */
static void sp_req_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static const struct sp_cp_sink sp_req_sink = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_req_timeout
};

static void sp_req_init (struct sp_req *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_xreq_init (&self->xreq, vfptr, fd);

    self->sink = &sp_req_sink;

    /*  Start assigning request IDs beginning with a random number. This way
        there should be no key clashes even if the executable is re-started. */
    sp_random_generate (&self->reqid, sizeof (self->reqid));

    self->flags = 0;
    self->resend_ivl = SP_REQ_DEFAULT_RESEND_IVL;
    sp_timer_init (&self->resend_timer, &self->sink,
        sp_sockbase_getcp (&self->xreq.sockbase));
}

static void sp_req_term (struct sp_req *self)
{
    if (self->flags & SP_REQ_INPROGRESS)
        sp_msg_term (&self->request);
    sp_timer_term (&self->resend_timer);
    sp_xreq_term (&self->xreq);
}

static void sp_req_destroy (struct sp_sockbase *self)
{
    struct sp_req *req;

    req = sp_cont (self, struct sp_req, xreq.sockbase);

    sp_req_term (req);
    sp_free (req);
}

static int sp_req_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_req *req;

    req = sp_cont (self, struct sp_req, xreq.sockbase);

    /*  If there's a request in progress, cancel it. */
    if (sp_slow (req->flags & SP_REQ_INPROGRESS)) {
        sp_msg_term (&req->request);
        req->flags &= ~SP_REQ_INPROGRESS;
    }

    /*  Generate new request ID for the new request and put it into message
        header. The most important bit is set to 1 to indicate that this is
        the bottom of the backtrace stack. */
    ++req->reqid;
    sp_assert (sp_chunkref_size (&msg->hdr) == 0);
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_init (&msg->hdr, 4);
    sp_putl (sp_chunkref_data (&msg->hdr), req->reqid | 0x80000000);

    /*  Store the message so that it can be re-sent if there's no reply. */
    sp_msg_cp (&req->request, msg);

    /*  Send the message. If it cannot be sent because of the pushback we'll
        pretend it was sent anyway. Re-send mechanism will take care of the
        rest. */
    rc = sp_xreq_send (&req->xreq.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    /*  Remember that we are processing a request and waiting for the reply
        at the moment. */
    req->flags |= SP_REQ_INPROGRESS;

    /*  Set up the re-send timer. This will also automatically cancel any
        running timer. */
    sp_timer_start (&req->resend_timer, req->resend_ivl);

    return 0;
}

static int sp_req_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_req *req;
    uint32_t reqid;

    req = sp_cont (self, struct sp_req, xreq.sockbase);

    /*  TODO: In case of invalid replies we should try to recv again here
        instead of returning -EAGAIN. */

    /*  No request was sent. Waiting for a reply doesn't make sense. */
    if (sp_slow (!(req->flags & SP_REQ_INPROGRESS)))
        return -EFSM;

    /*  Get new reply. */
    rc = sp_xreq_recv (&req->xreq.sockbase, msg);
    if (sp_slow (rc == -EAGAIN))
        return -EAGAIN;
    errnum_assert (rc == 0, -rc);

    /*  Ignore malformed replies. */
    if (sp_slow (sp_chunkref_size (&msg->hdr) != sizeof (uint32_t))) {
        sp_msg_term (msg);
        return -EAGAIN;
    }

    /*  Ignore replies with incorrect request IDs. */
    reqid = sp_getl (sp_chunkref_data (&msg->hdr));
    if (sp_slow (!(reqid & 0x80000000))) {
        sp_msg_term (msg);
        return -EAGAIN;
    }
    if (sp_slow (reqid != (req->reqid | 0x80000000))) {
        sp_msg_term (msg);
        return -EAGAIN;
    }

    /*  Trim the request ID. */
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_init (&msg->hdr, 0);

    /*  Clean-up. */
    sp_timer_stop (&req->resend_timer);
    sp_msg_term (&req->request);
    req->flags &= ~SP_REQ_INPROGRESS;

    return 0;
}

static int sp_req_setopt (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    struct sp_req *req;

    req = sp_cont (self, struct sp_req, xreq.sockbase);

    if (level != SP_REQ)
        return -ENOPROTOOPT;

    if (option == SP_RESEND_IVL) {
        if (sp_slow (optvallen != sizeof (int)))
            return -EINVAL;
        req->resend_ivl = *(int*) optval;
        return 0;
    }

    return -ENOPROTOOPT;
}

static int sp_req_getopt (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    struct sp_req *req;

    req = sp_cont (self, struct sp_req, xreq.sockbase);

    if (level != SP_REQ)
        return -ENOPROTOOPT;

    if (option == SP_RESEND_IVL) {
        if (sp_slow (*optvallen < sizeof (int)))
            return -EINVAL;
        *(int*) optval = req->resend_ivl;
        *optvallen = sizeof (int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static int sp_req_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (sp_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int sp_req_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

static void sp_req_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    int rc;
    struct sp_req *req;

    req = sp_cont (self, struct sp_req, sink);
    sp_assert (req->flags & SP_REQ_INPROGRESS);

    /*  Re-send the request. */
    rc = sp_xreq_send (&req->xreq.sockbase, &req->request);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);

    /*  Set up the next re-send timer. */
    sp_timer_start (&req->resend_timer, req->resend_ivl);
}

static struct sp_sockbase *sp_req_create (int fd)
{
    struct sp_req *self;

    self = sp_alloc (sizeof (struct sp_req), "socket (req)");
    alloc_assert (self);
    sp_req_init (self, &sp_req_sockbase_vfptr, fd);
    return &self->xreq.sockbase;
}

static struct sp_socktype sp_req_socktype_struct = {
    AF_SP,
    SP_REQ,
    sp_req_create
};

struct sp_socktype *sp_req_socktype = &sp_req_socktype_struct;

