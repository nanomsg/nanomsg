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

#include "../../nn.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define NN_REQ_DEFAULT_RESEND_IVL 60000

#define NN_REQ_INPROGRESS 1

struct nn_req {
    struct nn_xreq xreq;
    const struct nn_cp_sink *sink;
    uint32_t reqid;
    uint32_t flags;
    struct nn_msg request;
    int resend_ivl;
    struct nn_timer resend_timer;
};

/*  Private functions. */
static int nn_req_init (struct nn_req *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);
static void nn_req_term (struct nn_req *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_req_destroy (struct nn_sockbase *self);
static int nn_req_events (struct nn_sockbase *self);
static int nn_req_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_req_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_req_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_req_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static int nn_req_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen);
static int nn_req_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen);
static const struct nn_sockbase_vfptr nn_req_sockbase_vfptr = {
    nn_req_destroy,
    nn_xreq_add,
    nn_xreq_rm,
    nn_xreq_in,
    nn_xreq_out,
    nn_req_events,
    nn_req_send,
    nn_req_recv,
    nn_req_setopt,
    nn_req_getopt,
    nn_req_sethdr,
    nn_req_gethdr
};

/*  Event sink. */
static void nn_req_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer);
static const struct nn_cp_sink nn_req_sink = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_req_timeout
};

static int nn_req_init (struct nn_req *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    int rc;

    rc = nn_xreq_init (&self->xreq, vfptr, fd);
    if (rc < 0)
        return rc;

    self->sink = &nn_req_sink;

    /*  Start assigning request IDs beginning with a random number. This way
        there should be no key clashes even if the executable is re-started. */
    nn_random_generate (&self->reqid, sizeof (self->reqid));

    self->flags = 0;
    self->resend_ivl = NN_REQ_DEFAULT_RESEND_IVL;
    nn_timer_init (&self->resend_timer, &self->sink,
        nn_sockbase_getcp (&self->xreq.sockbase));

    return 0;
}

static void nn_req_term (struct nn_req *self)
{
    if (self->flags & NN_REQ_INPROGRESS)
        nn_msg_term (&self->request);
    nn_timer_term (&self->resend_timer);
    nn_xreq_term (&self->xreq);
}

static void nn_req_destroy (struct nn_sockbase *self)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    nn_req_term (req);
    nn_free (req);
}

static int nn_req_events (struct nn_sockbase *self)
{
    struct nn_req *req;
    int events;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    events = nn_xreq_events (&req->xreq.sockbase);
    if (!(req->flags & NN_REQ_INPROGRESS))
        events &= ~NN_SOCKBASE_EVENT_IN;
    return events;
}

static int nn_req_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  If there's a request in progress, cancel it. */
    if (nn_slow (req->flags & NN_REQ_INPROGRESS)) {
        nn_msg_term (&req->request);
        req->flags &= ~NN_REQ_INPROGRESS;
    }

    /*  Generate new request ID for the new request and put it into message
        header. The most important bit is set to 1 to indicate that this is
        the bottom of the backtrace stack. */
    ++req->reqid;
    nn_assert (nn_chunkref_size (&msg->hdr) == 0);
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_init (&msg->hdr, 4);
    nn_putl (nn_chunkref_data (&msg->hdr), req->reqid | 0x80000000);

    /*  Store the message so that it can be re-sent if there's no reply. */
    nn_msg_cp (&req->request, msg);

    /*  Send the message. If it cannot be sent because of the pushback we'll
        pretend it was sent anyway. Re-send mechanism will take care of the
        rest. */
    rc = nn_xreq_send (&req->xreq.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    /*  Remember that we are processing a request and waiting for the reply
        at the moment. */
    req->flags |= NN_REQ_INPROGRESS;

    /*  Set up the re-send timer. This will also automatically cancel any
        running timer. */
    nn_timer_start (&req->resend_timer, req->resend_ivl);

    return 0;
}

static int nn_req_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_req *req;
    uint32_t reqid;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  TODO: In case of invalid replies we should try to recv again here
        instead of returning -EAGAIN. */

    /*  No request was sent. Waiting for a reply doesn't make sense. */
    if (nn_slow (!(req->flags & NN_REQ_INPROGRESS)))
        return -EFSM;

    /*  Get new reply. */
    rc = nn_xreq_recv (&req->xreq.sockbase, msg);
    if (nn_slow (rc == -EAGAIN))
        return -EAGAIN;
    errnum_assert (rc == 0, -rc);

    /*  Ignore malformed replies. */
    if (nn_slow (nn_chunkref_size (&msg->hdr) != sizeof (uint32_t))) {
        nn_msg_term (msg);
        return -EAGAIN;
    }

    /*  Ignore replies with incorrect request IDs. */
    reqid = nn_getl (nn_chunkref_data (&msg->hdr));
    if (nn_slow (!(reqid & 0x80000000))) {
        nn_msg_term (msg);
        return -EAGAIN;
    }
    if (nn_slow (reqid != (req->reqid | 0x80000000))) {
        nn_msg_term (msg);
        return -EAGAIN;
    }

    /*  Trim the request ID. */
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_init (&msg->hdr, 0);

    /*  Clean-up. */
    nn_timer_stop (&req->resend_timer);
    nn_msg_term (&req->request);
    req->flags &= ~NN_REQ_INPROGRESS;

    return 0;
}

static int nn_req_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    if (level != NN_REQ)
        return -ENOPROTOOPT;

    if (option == NN_RESEND_IVL) {
        if (nn_slow (optvallen != sizeof (int)))
            return -EINVAL;
        req->resend_ivl = *(int*) optval;
        return 0;
    }

    return -ENOPROTOOPT;
}

static int nn_req_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    if (level != NN_REQ)
        return -ENOPROTOOPT;

    if (option == NN_RESEND_IVL) {
        if (nn_slow (*optvallen < sizeof (int)))
            return -EINVAL;
        *(int*) optval = req->resend_ivl;
        *optvallen = sizeof (int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static int nn_req_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (nn_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int nn_req_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

static void nn_req_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer)
{
    int rc;
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, sink);
    nn_assert (req->flags & NN_REQ_INPROGRESS);

    /*  Re-send the request. */
    rc = nn_xreq_send (&req->xreq.sockbase, &req->request);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);

    /*  Set up the next re-send timer. */
    nn_timer_start (&req->resend_timer, req->resend_ivl);
}

static int nn_req_create (int fd, struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_req *self;

    self = nn_alloc (sizeof (struct nn_req), "socket (req)");
    alloc_assert (self);
    rc = nn_req_init (self, &nn_req_sockbase_vfptr, fd);
    if (rc < 0) {
        nn_free (self);
        return rc;
    }
    *sockbase = &self->xreq.sockbase;

    return 0;
}

static struct nn_socktype nn_req_socktype_struct = {
    AF_SP,
    NN_REQ,
    nn_req_create
};

struct nn_socktype *nn_req_socktype = &nn_req_socktype_struct;

