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
#include "../../utils/list.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*  Default re-send interval is 1 minute. */
#define NN_REQ_DEFAULT_RESEND_IVL 60000

/*  There's no request being processed. */
#define NN_REQ_STATE_IDLE 0

/*  There's a request, but it haven't been sent to the peer(s) yet. */
#define NN_REQ_STATE_UNSENT 1

/*  Request was already sent. Waiting for the reply (or timeout). */
#define NN_REQ_STATE_SENT 2

/*  Reply was already received, but not yet retrieved by the user. */
#define NN_REQ_STATE_RECEIVED 3

struct nn_req {

    /*  The base class. Raw REQ socket. */
    struct nn_xreq xreq;

    /*  The callbacks. */
    const struct nn_cp_sink *sink;

    /*  ID of the request being currently processed. Replies for different
        requests are considered stale and simply dropped. */
    uint32_t reqid;

    /*  One of the states defined above. */
    int state;

    /*  Stored request, so that it can be re-sent if needed. This field is
        valid while in UNSENT and SENT states. */
    struct nn_msg request;

    /*  Stored reply, so that user can retrieve it later on. This field is
        valid while in RECEIVED state. */
    struct nn_msg reply;

    /*  Re-send interval, in milliseconds. */
    int resend_ivl;

    /*  Timer used to wait till request resending should be done. */
    struct nn_timer resend_timer;
};

/*  Private functions. */
static int nn_req_init (struct nn_req *self,
    const struct nn_sockbase_vfptr *vfptr);
static void nn_req_term (struct nn_req *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_req_destroy (struct nn_sockbase *self);
static void nn_req_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_req_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_req_events (struct nn_sockbase *self);
static int nn_req_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_req_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_req_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_req_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_req_sockbase_vfptr = {
    0,
    nn_xreq_ispeer,
    nn_req_destroy,
    nn_xreq_add,
    nn_xreq_rm,
    nn_req_in,
    nn_req_out,
    nn_req_events,
    nn_req_send,
    nn_req_recv,
    nn_req_setopt,
    nn_req_getopt
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
    const struct nn_sockbase_vfptr *vfptr)
{
    int rc;

    rc = nn_xreq_init (&self->xreq, vfptr);
    if (rc < 0)
        return rc;

    self->sink = &nn_req_sink;

    /*  Start assigning request IDs beginning with a random number. This way
        there should be no key clashes even if the executable is re-started. */
    nn_random_generate (&self->reqid, sizeof (self->reqid));

    self->state = NN_REQ_STATE_IDLE;
    self->resend_ivl = NN_REQ_DEFAULT_RESEND_IVL;
    nn_timer_init (&self->resend_timer, &self->sink,
        nn_sockbase_getcp (&self->xreq.sockbase));

    return 0;
}

static void nn_req_term (struct nn_req *self)
{
    if (self->state == NN_REQ_STATE_UNSENT || self->state == NN_REQ_STATE_SENT)
        nn_msg_term (&self->request);
    if (self->state == NN_REQ_STATE_RECEIVED)
        nn_msg_term (&self->reply);
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

static void nn_req_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_req *req;
    uint32_t reqid;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  Pass the pipe to the raw REQ socket. */
    nn_xreq_in (&req->xreq.sockbase, pipe);

    while (1) {

        /*  Get new reply. */
        rc = nn_xreq_recv (&req->xreq.sockbase, &req->reply);
        if (nn_slow (rc == -EAGAIN))
            return;
        errnum_assert (rc == 0, -rc);

        /*  No request was sent. Getting a reply doesn't make sense. */
        if (nn_slow (req->state != NN_REQ_STATE_SENT)) {
            nn_msg_term (&req->reply);
            continue;
        }

        /*  Ignore malformed replies. */
        if (nn_slow (nn_chunkref_size (&req->reply.hdr) != sizeof (uint32_t))) {
            nn_msg_term (&req->reply);
            continue;
        }

        /*  Ignore replies with incorrect request IDs. */
        reqid = nn_getl (nn_chunkref_data (&req->reply.hdr));
        if (nn_slow (!(reqid & 0x80000000))) {
            nn_msg_term (&req->reply);
            continue;
        }
        if (nn_slow (reqid != (req->reqid | 0x80000000))) {
            nn_msg_term (&req->reply);
            continue;
        }

        /*  Trim the request ID. */
        nn_chunkref_term (&req->reply.hdr);
        nn_chunkref_init (&req->reply.hdr, 0);

        /*  Swtich to RECEIVED state. */
        nn_timer_stop (&req->resend_timer);
        nn_msg_term (&req->request);
        req->state = NN_REQ_STATE_RECEIVED;

        return;
    }
}

static void nn_req_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_req *req;
    struct nn_msg msg;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  Add the pipe to the underlying raw socket. */
    nn_xreq_out (&req->xreq.sockbase, pipe);

    /*  If the current request was not sent yet, send it now. */
    if (req->state == NN_REQ_STATE_UNSENT) {
        nn_msg_cp (&msg, &req->request);
        rc = nn_xreq_send (&req->xreq.sockbase, &msg);
        errnum_assert (rc == 0, -rc);
        req->state = NN_REQ_STATE_SENT;
    }
}

static int nn_req_events (struct nn_sockbase *self)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  OUT is signalled all the time because sending a request while
        another one is being processed cancels the old one. */
    if (req->state == NN_REQ_STATE_RECEIVED)
        return NN_SOCKBASE_EVENT_IN | NN_SOCKBASE_EVENT_OUT;
    return NN_SOCKBASE_EVENT_OUT;
}

static int nn_req_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  If there's a request in progress, cancel it. */
    if (nn_slow (req->state != NN_REQ_STATE_IDLE)) {
        if (req->state == NN_REQ_STATE_UNSENT ||
              req->state == NN_REQ_STATE_SENT)
            nn_msg_term (&req->request);
        if (req->state == NN_REQ_STATE_RECEIVED)
            nn_msg_term (&req->reply);
        nn_timer_term (&req->resend_timer);
        req->state = NN_REQ_STATE_IDLE;
    }

    /*  Generate new request ID for the new request and put it into message
        header. The most important bit is set to 1 to indicate that this is
        the bottom of the backtrace stack. */
    ++req->reqid;
    nn_assert (nn_chunkref_size (&msg->hdr) == 0);
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_init (&msg->hdr, 4);
    nn_putl (nn_chunkref_data (&msg->hdr), req->reqid | 0x80000000);

    /*  Store the message so that it can be re-sent if there's no reply. 
        Then make a copy of it and send it. */
    nn_msg_cp (&req->request, msg);
    rc = nn_xreq_send (&req->xreq.sockbase, msg);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);

    /*  If the request cannot be sent at the moment switch to UNSENT state.
        It will be sent as soon as a new outbound pipe arrives. */
    if (nn_slow (rc == -EAGAIN)) {
        nn_msg_term (msg);
        req->state = NN_REQ_STATE_UNSENT;
        return 0;
    }

    /*  If the request was successgfully sent set up the re-send timer in case
        it get lost somewhere further out in the topology. */
    nn_timer_start (&req->resend_timer, req->resend_ivl);
    req->state = NN_REQ_STATE_SENT;

    return 0;
}

static int nn_req_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    /*  No request was sent. Waiting for a reply doesn't make sense. */
    if (nn_slow (req->state == NN_REQ_STATE_IDLE))
        return -EFSM;

    /*  If reply was not yet recieved, wait further. */
    if (nn_slow (req->state != NN_REQ_STATE_RECEIVED))
        return -EAGAIN;

    /*  If the reply was already received, just pass it to the caller. */
    nn_msg_mv (msg, &req->reply);
    req->state = NN_REQ_STATE_IDLE;

    return 0;
}

static int nn_req_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    struct nn_req *req;

    req = nn_cont (self, struct nn_req, xreq.sockbase);

    if (level != NN_REQ)
        return -ENOPROTOOPT;

    if (option == NN_REQ_RESEND_IVL) {
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

    if (option == NN_REQ_RESEND_IVL) {
        if (nn_slow (*optvallen < sizeof (int)))
            return -EINVAL;
        *(int*) optval = req->resend_ivl;
        *optvallen = sizeof (int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static void nn_req_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer)
{
    int rc;
    struct nn_req *req;
    struct nn_msg msg;

    req = nn_cont (self, struct nn_req, sink);
    nn_assert (req->state == NN_REQ_STATE_SENT);

    /*  Re-send the request. If it cannot be sent, just drop it. */
    nn_msg_cp (&msg, &req->request);
    rc = nn_xreq_send (&req->xreq.sockbase, &msg);
    errnum_assert (rc == 0 || rc == -EAGAIN, -rc);
    if (nn_slow (rc == -EAGAIN))
        nn_msg_term (&msg);

    /*  Set up the next re-send timer. */
    nn_timer_start (&req->resend_timer, req->resend_ivl);
}

static int nn_req_create (struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_req *self;

    self = nn_alloc (sizeof (struct nn_req), "socket (req)");
    alloc_assert (self);
    rc = nn_req_init (self, &nn_req_sockbase_vfptr);
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
    nn_req_create,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_req_socktype = &nn_req_socktype_struct;

