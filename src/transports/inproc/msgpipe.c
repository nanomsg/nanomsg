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

/*  Private functions. */
static void nn_msgpipe_term (struct nn_msgpipe *self);

/*  Implementation of both nn_pipe interfaces. */
static int nn_msgpipe_send0 (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_msgpipe_recv0 (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_msgpipe_send1 (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_msgpipe_recv1 (struct nn_pipebase *self, struct nn_msg *msg);
static const struct nn_pipebase_vfptr nn_msgpipe_vfptr0 =
    {nn_msgpipe_send0, nn_msgpipe_recv0};
static const struct nn_pipebase_vfptr nn_msgpipe_vfptr1 =
    {nn_msgpipe_send1, nn_msgpipe_recv1};

/*  Implementation of event callbacks. */
static void nn_msgpipe_inevent0 (const struct nn_cp_sink **self,
    struct nn_event *event);
static const struct nn_cp_sink nn_msgpipe_inevent0_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, nn_msgpipe_inevent0};
static const struct nn_cp_sink *nn_msgpipe_inevent0_sinkptr =
    &nn_msgpipe_inevent0_sink;
static void nn_msgpipe_inevent1 (const struct nn_cp_sink **self,
    struct nn_event *event);
static const struct nn_cp_sink nn_msgpipe_inevent1_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, nn_msgpipe_inevent1};
static const struct nn_cp_sink *nn_msgpipe_inevent1_sinkptr =
    &nn_msgpipe_inevent1_sink;
static void nn_msgpipe_outevent0 (const struct nn_cp_sink **self,
    struct nn_event *event);
static const struct nn_cp_sink nn_msgpipe_outevent0_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, nn_msgpipe_outevent0};
static const struct nn_cp_sink *nn_msgpipe_outevent0_sinkptr =
    &nn_msgpipe_outevent0_sink;
static void nn_msgpipe_outevent1 (const struct nn_cp_sink **self,
    struct nn_event *event);
static const struct nn_cp_sink nn_msgpipe_outevent1_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, nn_msgpipe_outevent1};
static const struct nn_cp_sink *nn_msgpipe_outevent1_sinkptr =
    &nn_msgpipe_outevent1_sink;

void nn_msgpipe_init (struct nn_msgpipe *self,
    struct nn_inprocb *inprocb, struct nn_inprocc *inprocc)
{
    /*  Initialise the pipes. */ 
    nn_pipebase_init (&self->pipes [0], &nn_msgpipe_vfptr0, &inprocb->epbase);
    nn_pipebase_init (&self->pipes [1], &nn_msgpipe_vfptr1, &inprocc->epbase);
    self->flags = NN_MSGPIPE_PIPE0_ACTIVE | NN_MSGPIPE_PIPE1_ACTIVE;

    /*  Initialise message queues for both directions. */
    /*  TODO: Set up proper queue limits. */
    nn_msgqueue_init (&(self->queues [0]), 1000000);
    nn_msgqueue_init (&(self->queues [1]), 1000000);

    /*  Initlaise the events to communicate between peers. */
    nn_event_init (&self->inevents [0], &nn_msgpipe_inevent0_sinkptr,
        nn_pipebase_getcp (&self->pipes [0]));
    nn_event_init (&self->inevents [1], &nn_msgpipe_inevent1_sinkptr,
        nn_pipebase_getcp (&self->pipes [1]));
    nn_event_init (&self->outevents [0], &nn_msgpipe_outevent0_sinkptr,
        nn_pipebase_getcp (&self->pipes [0]));
    nn_event_init (&self->outevents [1], &nn_msgpipe_outevent1_sinkptr,
        nn_pipebase_getcp (&self->pipes [1]));

    /*  Let the endpoints know they own this pipe. */
    nn_inprocb_add_pipe (inprocb, self);
    nn_inprocc_add_pipe (inprocc, self);

    /*  Mark the pipe as writeable in both directions. */
    nn_pipebase_activate (&self->pipes [0]);
    nn_pipebase_activate (&self->pipes [1]);
}

void nn_msgpipe_detachb (struct nn_msgpipe *self)
{
    /*  Detach the pipe on the bound end. */
    nn_assert (self->flags & NN_MSGPIPE_PIPE0_ACTIVE);
    nn_pipebase_term (&self->pipes [0]);
    self->flags &= ~NN_MSGPIPE_PIPE0_ACTIVE;

    /* If there are no more references deallocate the pipe. */
    if (!self->flags)
        nn_msgpipe_term (self);
}

void nn_msgpipe_detachc (struct nn_msgpipe *self)
{
    /*  Detach the pipe on the bound end. */
    nn_assert (self->flags & NN_MSGPIPE_PIPE1_ACTIVE);
    nn_pipebase_term (&self->pipes [1]);
    self->flags &= ~NN_MSGPIPE_PIPE1_ACTIVE;

    /* If there are no more references deallocate the pipe. */
    if (!self->flags)
        nn_msgpipe_term (self);
}

static void nn_msgpipe_term (struct nn_msgpipe *self)
{
    /*  Deallocate the message queues along with the messages stored therein. */
    nn_msgqueue_term (&(self->queues [1]));
    nn_msgqueue_term (&(self->queues [0]));

    nn_event_term (&self->inevents [0]);
    nn_event_term (&self->inevents [1]);
    nn_event_term (&self->outevents [0]);
    nn_event_term (&self->outevents [1]);

    /*  The lifetime of this object is managed by reference count (number
        of endpoints having reference to it) not by a particular owner. Thus,
        the object has to deallocate itself once there are no more
        references. */
    nn_free (self);
}

static int nn_msgpipe_send0 (struct nn_pipebase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, pipes [0]);
    rc = nn_msgqueue_send (&msgpipe->queues [0], msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_MSGQUEUE_SIGNAL)
        nn_event_signal (&msgpipe->inevents [1]);
    
    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_sent (self);

    return 0;
}

static int nn_msgpipe_recv0 (struct nn_pipebase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, pipes [0]);
    rc = nn_msgqueue_recv (&msgpipe->queues [1], msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_MSGQUEUE_SIGNAL)
        nn_event_signal (&msgpipe->outevents [1]);

    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_received (self);

    return NN_PIPE_PARSED;
}

static int nn_msgpipe_send1 (struct nn_pipebase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, pipes [1]);
    rc = nn_msgqueue_send (&msgpipe->queues [1], msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_MSGQUEUE_SIGNAL)
        nn_event_signal (&msgpipe->inevents [0]);

    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_sent (self);

    return 0;
}

static int nn_msgpipe_recv1 (struct nn_pipebase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (self, struct nn_msgpipe, pipes [1]);
    rc = nn_msgqueue_recv (&msgpipe->queues [0], msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_MSGQUEUE_SIGNAL)
        nn_event_signal (&msgpipe->outevents [0]);

    if (!(rc & NN_MSGQUEUE_RELEASE))
        nn_pipebase_received (self);

    return NN_PIPE_PARSED;
}

static void nn_msgpipe_inevent0 (const struct nn_cp_sink **self,
    struct nn_event *event)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (event, struct nn_msgpipe, inevents [0]);
    nn_pipebase_received (&msgpipe->pipes [0]);
}

static void nn_msgpipe_inevent1 (const struct nn_cp_sink **self,
    struct nn_event *event)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (event, struct nn_msgpipe, inevents [1]);
    nn_pipebase_received (&msgpipe->pipes [1]);
}

static void nn_msgpipe_outevent0 (const struct nn_cp_sink **self,
    struct nn_event *event)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (event, struct nn_msgpipe, outevents [0]);
    nn_pipebase_sent (&msgpipe->pipes [0]);
}

static void nn_msgpipe_outevent1 (const struct nn_cp_sink **self,
    struct nn_event *event)
{
    struct nn_msgpipe *msgpipe;

    msgpipe = nn_cont (event, struct nn_msgpipe, outevents [1]);
    nn_pipebase_sent (&msgpipe->pipes [1]);
}

