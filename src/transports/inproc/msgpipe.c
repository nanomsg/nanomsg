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
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "msgpipe.h"
#include "inprocb.h"
#include "inprocc.h"

#include "../../protocol.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/msg.h"

#include <stddef.h>

/*  Forward declarations for nn_msgpipehalf class. */
static void nn_msgpipehalf_init (struct nn_msgpipehalf *self,
    const struct nn_pipebase_vfptr *vfptr,
    struct nn_epbase *epbase, struct nn_epbase *peer_epbase,
    void (*rmpipefn) (struct nn_msgpipehalf *self));
static void nn_msgpipehalf_term (struct nn_msgpipehalf *self);
static void nn_msgpipehalf_detach (struct nn_msgpipehalf *self);
static void nn_msgpipehalf_send (struct nn_msgpipehalf *self,
    struct nn_msgpipehalf *peer, struct nn_msg *msg);
static void nn_msgpipehalf_recv (struct nn_msgpipehalf *self,
    struct nn_msgpipehalf *peer, struct nn_msg *msg);

/******************************************************************************/
/*  Implementation of nn_msgpipe.                                             */
/******************************************************************************/

/*  Private functions. */
static void nn_msgpipe_destroy (struct nn_msgpipe *self);
static void nn_msgpipe_rmpipeb (struct nn_msgpipehalf *self);
static void nn_msgpipe_rmpipec (struct nn_msgpipehalf *self);

/*  Implementation of nn_pipe interface for the bound half. */
static int nn_msgpipe_sendb (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_msgpipe_recvb (struct nn_pipebase *self, struct nn_msg *msg);
static const struct nn_pipebase_vfptr nn_msgpipe_vfptrb =
    {nn_msgpipe_sendb, nn_msgpipe_recvb};

/*  Implementation of nn_pipe interface for the connected half. */
static int nn_msgpipe_sendc (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_msgpipe_recvc (struct nn_pipebase *self, struct nn_msg *msg);
static const struct nn_pipebase_vfptr nn_msgpipe_vfptrc =
    {nn_msgpipe_sendc, nn_msgpipe_recvc};

void nn_msgpipe_init (struct nn_msgpipe *self,
    struct nn_inprocb *inprocb, struct nn_inprocc *inprocc)
{
    nn_mutex_init (&self->sync);

    self->flags = 0;

    /*  Initialise the halfs of the pipe. */ 
    nn_msgpipehalf_init (&self->bhalf, &nn_msgpipe_vfptrb, &inprocb->epbase,
        &inprocc->epbase, nn_msgpipe_rmpipeb);
    nn_msgpipehalf_init (&self->chalf, &nn_msgpipe_vfptrc, &inprocc->epbase,
        &inprocb->epbase, nn_msgpipe_rmpipec);

    nn_list_item_init (&self->item);

    /*  Store the references to the endpoints. */
    self->inprocb = inprocb;
    self->inprocc = inprocc;

    /*  Attach the pipe to both endpoints. */
    nn_inprocb_add_pipe (inprocb, self);
    nn_inprocc_add_pipe (inprocc, self);
}

static void nn_msgpipe_destroy (struct nn_msgpipe *self)
{
    /*  The precondition is that both halfs are already terminated at
        this point. We don't need to terminate them here. */
    nn_assert (self->flags & NN_MSGPIPE_FLAG_BHALF_DEAD &&
        self->flags & NN_MSGPIPE_FLAG_CHALF_DEAD);

    /*  Deallocate the resources. */
    nn_mutex_term (&self->sync);
    nn_list_item_term (&self->item);
    nn_free (self);
}

void nn_msgpipe_detachb (struct nn_msgpipe *self)
{
    nn_mutex_lock (&self->sync);
    nn_assert (!(self->flags & NN_MSGPIPE_FLAG_BHALF_DEAD));
    nn_msgpipehalf_detach (&self->bhalf);
    nn_mutex_unlock (&self->sync);
}

void nn_msgpipe_detachc (struct nn_msgpipe *self)
{
    nn_mutex_lock (&self->sync);
    nn_assert (!(self->flags & NN_MSGPIPE_FLAG_CHALF_DEAD));
    nn_msgpipehalf_detach (&self->chalf);
    nn_mutex_unlock (&self->sync);
}

static void nn_msgpipe_rmpipeb (struct nn_msgpipehalf *self)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, bhalf);

    /*  Terminate the bound half of the pipe. */
    nn_msgpipehalf_term (&msgpipe->bhalf);
    msgpipe->flags |= NN_MSGPIPE_FLAG_BHALF_DEAD;

    /*  Remove the pipe from the endpoint. */
    nn_inprocb_rm_pipe (msgpipe->inprocb, msgpipe);

    /*  If both ends of the pipe are detached, deallocate it. */
    if (msgpipe->flags & NN_MSGPIPE_FLAG_CHALF_DEAD)
        nn_msgpipe_destroy (msgpipe);
}

static void nn_msgpipe_rmpipec (struct nn_msgpipehalf *self)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, chalf);

    /*  Terminate the bound half of the pipe. */
    nn_msgpipehalf_term (&msgpipe->chalf);
    msgpipe->flags |= NN_MSGPIPE_FLAG_CHALF_DEAD;

    /*  Remove the pipe from the endpoint. */
    nn_inprocc_rm_pipe (msgpipe->inprocc, msgpipe);

    /*  If both ends of the pipe are detached, deallocate it. */
    if (msgpipe->flags & NN_MSGPIPE_FLAG_BHALF_DEAD)
        nn_msgpipe_destroy (msgpipe);
}

static int nn_msgpipe_sendb (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, bhalf.pipebase);

    nn_mutex_lock (&msgpipe->sync);
    nn_assert (!(msgpipe->flags & NN_MSGPIPE_FLAG_BHALF_DEAD));
    if (nn_slow (msgpipe->flags & NN_MSGPIPE_FLAG_CHALF_DEAD)) {
        nn_mutex_unlock (&msgpipe->sync);
        return -EAGAIN;
    }
    nn_msgpipehalf_send (&msgpipe->bhalf, &msgpipe->chalf, msg);
    nn_mutex_unlock (&msgpipe->sync);

    return 0;
}

static int nn_msgpipe_recvb (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, bhalf.pipebase);

    nn_mutex_lock (&msgpipe->sync);
    nn_assert (!(msgpipe->flags & NN_MSGPIPE_FLAG_BHALF_DEAD));
    nn_msgpipehalf_recv (&msgpipe->bhalf, msgpipe->flags &
        NN_MSGPIPE_FLAG_CHALF_DEAD ? NULL : &msgpipe->chalf, msg);
    nn_mutex_unlock (&msgpipe->sync);

    return NN_PIPEBASE_PARSED;
}

static int nn_msgpipe_sendc (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, chalf.pipebase);

    nn_mutex_lock (&msgpipe->sync);
    nn_assert (!(msgpipe->flags & NN_MSGPIPE_FLAG_CHALF_DEAD));
    if (nn_slow (msgpipe->flags & NN_MSGPIPE_FLAG_BHALF_DEAD)) {
        nn_mutex_unlock (&msgpipe->sync);
        return -EAGAIN;
    }
    nn_msgpipehalf_send (&msgpipe->chalf, &msgpipe->bhalf, msg);
    nn_mutex_unlock (&msgpipe->sync);

    return 0;
}

static int nn_msgpipe_recvc (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, chalf.pipebase);

    nn_mutex_lock (&msgpipe->sync);
    nn_assert (!(msgpipe->flags & NN_MSGPIPE_FLAG_CHALF_DEAD));
    nn_msgpipehalf_recv (&msgpipe->chalf, msgpipe->flags &
        NN_MSGPIPE_FLAG_BHALF_DEAD ? NULL : &msgpipe->bhalf, msg);
    nn_mutex_unlock (&msgpipe->sync);

    return NN_PIPEBASE_PARSED;
}

/******************************************************************************/
/*  Implementation of nn_msgpipehalf.                                         */
/******************************************************************************/

/*  Implementation of event sink. */
static void nn_msgpipehalf_event (const struct nn_cp_sink **self,
    struct nn_event *event);
static const struct nn_cp_sink nn_msgpipehalf_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, nn_msgpipehalf_event};

static void nn_msgpipehalf_init (struct nn_msgpipehalf *self,
    const struct nn_pipebase_vfptr *vfptr,
    struct nn_epbase *epbase, struct nn_epbase *peer_epbase,
    void (*rmpipefn) (struct nn_msgpipehalf *self))
{
    int rcvbuf;
    int sndbuf;
    size_t sz;
    struct nn_cp *cp;

    /*  Initialise the base class. */ 
    nn_pipebase_init (&self->pipebase, vfptr, epbase);

    self->flags = 0;

    /*  Get buffer sizes. */
    sz = sizeof (rcvbuf);
    nn_epbase_getopt (epbase, NN_SOL_SOCKET, NN_RCVBUF, &rcvbuf, &sz);
    nn_assert (sz == sizeof (rcvbuf));
    sz = sizeof (sndbuf);
    nn_epbase_getopt (peer_epbase, NN_SOL_SOCKET, NN_SNDBUF, &sndbuf, &sz);
    nn_assert (sz == sizeof (sndbuf));

    /*  Initialise inbound message queue. */
    nn_msgqueue_init (&(self->queue), sndbuf + rcvbuf);

    /*  Set the sink for all async events. */
    self->sink = &nn_msgpipehalf_sink;

    /*  Initlaise the async events. */
    cp = nn_pipebase_getcp (&self->pipebase);
    nn_event_init (&self->inevent, &self->sink, cp);
    nn_event_init (&self->outevent, &self->sink, cp);
    nn_event_init (&self->detachevent, &self->sink, cp);

    self->rmpipefn = rmpipefn;

    /*  Mark the pipe as writeable. */
    nn_pipebase_activate (&self->pipebase);
}

static void nn_msgpipehalf_term (struct nn_msgpipehalf *self)
{
    /*  Terminate the async events. */
    nn_event_term (&self->inevent);
    nn_event_term (&self->outevent);
    nn_event_term (&self->detachevent);
 
    /*  Deallocate the message queue along with the messages stored in it. */
    nn_msgqueue_term (&self->queue);

    /*  Terminate the base class. */
    nn_pipebase_term (&self->pipebase);
}

static void nn_msgpipehalf_detach (struct nn_msgpipehalf *self)
{
    /*  If detachment is already underway, do nothing. */
    if (self->flags == NN_MSGPIPEHALF_FLAG_DETACHING)
        return;

    /*  If still attached, fire the detach event. */
    self->flags |= NN_MSGPIPEHALF_FLAG_DETACHING;
    nn_event_signal (&self->detachevent);
}

static void nn_msgpipehalf_send (struct nn_msgpipehalf *self,
    struct nn_msgpipehalf *peer, struct nn_msg *msg)
{
    int rc;

    /*  Put the message to the peer's inbound queue. */
    rc = nn_msgqueue_send (&peer->queue, msg);
    errnum_assert (rc >= 0, -rc);

    /*  If the peer is sleeping, wake it up. */
    if (rc & NN_MSGQUEUE_SIGNAL)
        nn_event_signal (&peer->inevent);

    /*  If the pipe is still writeable, make sure that it's not removed
        from the list of eligible outbound pipes. */
    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_sent (&self->pipebase);
}

static void nn_msgpipehalf_recv (struct nn_msgpipehalf *self,
    struct nn_msgpipehalf *peer, struct nn_msg *msg)
{
    int rc;

    /*  Get a message from the inbound queue. */
    rc = nn_msgqueue_recv (&self->queue, msg);
    errnum_assert (rc >= 0, -rc);

    /*  If it makes the other end writeable, notify the peer. */
    if (rc & NN_MSGQUEUE_SIGNAL && peer)
        nn_event_signal (&peer->outevent);

    /*  If the pipe is still readable, make sure that it's not removed
        from the list of eligible inbound pipes. */
    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_received (&self->pipebase);
}

static void nn_msgpipehalf_event (const struct nn_cp_sink **self,
    struct nn_event *event)
{
    struct nn_msgpipehalf *half;

    half = nn_cont (self, struct nn_msgpipehalf, sink);

    /*  inevent handler. */
    if (event == &half->inevent) {
        nn_pipebase_received (&half->pipebase);
        return;
    }

    /*  outevent handler. */
    if (event == &half->outevent) {
        nn_pipebase_sent (&half->pipebase);
        return;
    }

    /*  detachevent handler. Remove the pipe from the endpoint. Be aware that
        this function may also deallocate the pipe itself. */
    if (event == &half->detachevent) {
        nn_assert (half->flags & NN_MSGPIPEHALF_FLAG_DETACHING);
        half->rmpipefn (half);
        return;
    }

    /*  Unexpected event. */
    nn_assert (0);
}

