/*
    Copyright (c) 2012 250bpm s.r.o.

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

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <stddef.h>

/*  Private functions. */
static void sp_msgpipe_term (struct sp_msgpipe *self);

/*  Implementation of both sp_pipe interfaces. */
static int sp_msgpipe_send0 (struct sp_pipebase *self,
    const void *buf, size_t len);
static int sp_msgpipe_recv0 (struct sp_pipebase *self,
    void *buf, size_t *len);
static int sp_msgpipe_send1 (struct sp_pipebase *self,
    const void *buf, size_t len);
static int sp_msgpipe_recv1 (struct sp_pipebase *self,
    void *buf, size_t *len);
static const struct sp_pipebase_vfptr sp_msgpipe_vfptr0 =
    {sp_msgpipe_send0, sp_msgpipe_recv0};
static const struct sp_pipebase_vfptr sp_msgpipe_vfptr1 =
    {sp_msgpipe_send1, sp_msgpipe_recv1};

/*  Implementation of event callbacks. */
static void sp_msgpipe_inevent0 (const struct sp_sink **self,
    struct sp_event *event);
static const struct sp_sink sp_msgpipe_inevent0_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, sp_msgpipe_inevent0};
static const struct sp_sink *sp_msgpipe_inevent0_sinkptr =
    &sp_msgpipe_inevent0_sink;
static void sp_msgpipe_inevent1 (const struct sp_sink **self,
    struct sp_event *event);
static const struct sp_sink sp_msgpipe_inevent1_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, sp_msgpipe_inevent1};
static const struct sp_sink *sp_msgpipe_inevent1_sinkptr =
    &sp_msgpipe_inevent1_sink;
static void sp_msgpipe_outevent0 (const struct sp_sink **self,
    struct sp_event *event);
static const struct sp_sink sp_msgpipe_outevent0_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, sp_msgpipe_outevent0};
static const struct sp_sink *sp_msgpipe_outevent0_sinkptr =
    &sp_msgpipe_outevent0_sink;
static void sp_msgpipe_outevent1 (const struct sp_sink **self,
    struct sp_event *event);
static const struct sp_sink sp_msgpipe_outevent1_sink =
    {NULL, NULL, NULL, NULL, NULL, NULL, sp_msgpipe_outevent1};
static const struct sp_sink *sp_msgpipe_outevent1_sinkptr =
    &sp_msgpipe_outevent1_sink;

void sp_msgpipe_init (struct sp_msgpipe *self,
    struct sp_inprocb *inprocb, struct sp_inprocc *inprocc)
{
    /*  Initialise the pipes. */ 
    sp_pipebase_init (&self->pipes [0], &sp_msgpipe_vfptr0, &inprocb->epbase);
    sp_pipebase_init (&self->pipes [1], &sp_msgpipe_vfptr1, &inprocc->epbase);
    self->flags = SP_MSGPIPE_PIPE0_ACTIVE | SP_MSGPIPE_PIPE1_ACTIVE;

    /*  Initialise message queues for both directions. */
    /*  TODO: Set up proper queue limits. */
    sp_msgqueue_init (&(self->queues [0]), 1000000);
    sp_msgqueue_init (&(self->queues [1]), 1000000);

    /*  Initlaise the events to communicate between peers. */
    sp_event_init (&self->inevents [0], &sp_msgpipe_inevent0_sinkptr,
        sp_pipebase_getcp (&self->pipes [0]));
    sp_event_init (&self->inevents [1], &sp_msgpipe_inevent1_sinkptr,
        sp_pipebase_getcp (&self->pipes [1]));
    sp_event_init (&self->outevents [0], &sp_msgpipe_outevent0_sinkptr,
        sp_pipebase_getcp (&self->pipes [0]));
    sp_event_init (&self->outevents [1], &sp_msgpipe_outevent1_sinkptr,
        sp_pipebase_getcp (&self->pipes [1]));

    /*  Let the endpoints know they own this pipe. */
    sp_inprocb_add_pipe (inprocb, self);
    sp_inprocc_add_pipe (inprocc, self);

    /*  Mark the pipe as writeable in both directions. */
    sp_pipebase_out (&self->pipes [0]);
    sp_pipebase_out (&self->pipes [1]);
}

void sp_msgpipe_detachb (struct sp_msgpipe *self)
{
    /*  Detach the pipe on the bound end. */
    sp_assert (self->flags & SP_MSGPIPE_PIPE0_ACTIVE);
    sp_pipebase_term (&self->pipes [0]);
    self->flags &= ~SP_MSGPIPE_PIPE0_ACTIVE;

    /* If there are no more references deallocate the pipe. */
    if (!self->flags)
        sp_msgpipe_term (self);
}

void sp_msgpipe_detachc (struct sp_msgpipe *self)
{
    /*  Detach the pipe on the bound end. */
    sp_assert (self->flags & SP_MSGPIPE_PIPE1_ACTIVE);
    sp_pipebase_term (&self->pipes [1]);
    self->flags &= ~SP_MSGPIPE_PIPE1_ACTIVE;

    /* If there are no more references deallocate the pipe. */
    if (!self->flags)
        sp_msgpipe_term (self);
}

static void sp_msgpipe_term (struct sp_msgpipe *self)
{
    /*  Deallocate the message queues along with the messages stored therein. */
    sp_msgqueue_term (&(self->queues [1]));
    sp_msgqueue_term (&(self->queues [0]));

    sp_event_term (&self->inevents [0]);
    sp_event_term (&self->inevents [1]);
    sp_event_term (&self->outevents [0]);
    sp_event_term (&self->outevents [1]);

    /*  The lifetime of this object is managed by reference count (number
        of endpoints having reference to it) not by a particular owner. Thus,
        the object has to deallocate itself once there are no more
        references. */
    sp_free (self);
}

static int sp_msgpipe_send0 (struct sp_pipebase *self,
    const void *buf, size_t len)
{
    int rc;
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (self, struct sp_msgpipe, pipes [0]);
    rc = sp_msgqueue_send (&msgpipe->queues [0], buf, len);
    errnum_assert (rc >= 0, -rc);

    if (!(rc & SP_MSGQUEUE_SIGNAL))
        return rc;

    sp_event_signal (&msgpipe->inevents [1]);
    rc &= ~SP_MSGQUEUE_SIGNAL;
    return rc;
}

static int sp_msgpipe_recv0 (struct sp_pipebase *self,
    void *buf, size_t *len)
{
    int rc;
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (self, struct sp_msgpipe, pipes [0]);
    rc = sp_msgqueue_recv (&msgpipe->queues [1], buf, len);
    errnum_assert (rc >= 0, -rc);

    if (!(rc & SP_MSGQUEUE_SIGNAL))
        return rc;

    sp_event_signal (&msgpipe->outevents [1]);
    rc &= ~SP_MSGQUEUE_SIGNAL;
    return rc;
}

static int sp_msgpipe_send1 (struct sp_pipebase *self,
    const void *buf, size_t len)
{
    int rc;
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (self, struct sp_msgpipe, pipes [1]);
    rc = sp_msgqueue_send (&msgpipe->queues [1], buf, len);
    errnum_assert (rc >= 0, -rc);

    if (!(rc & SP_MSGQUEUE_SIGNAL))
        return rc;

    sp_event_signal (&msgpipe->inevents [0]);
    rc &= ~SP_MSGQUEUE_SIGNAL;
    return rc;
}

static int sp_msgpipe_recv1 (struct sp_pipebase *self,
    void *buf, size_t *len)
{
    int rc;
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (self, struct sp_msgpipe, pipes [1]);
    rc = sp_msgqueue_recv (&msgpipe->queues [0], buf, len);
    errnum_assert (rc >= 0, -rc);

    if (!(rc & SP_MSGQUEUE_SIGNAL))
        return rc;

    sp_event_signal (&msgpipe->outevents [0]);
    rc &= ~SP_MSGQUEUE_SIGNAL;
    return rc;
}

static void sp_msgpipe_inevent0 (const struct sp_sink **self,
    struct sp_event *event)
{
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (event, struct sp_msgpipe, inevents [0]);
    sp_pipebase_in (&msgpipe->pipes [0]);
}

static void sp_msgpipe_inevent1 (const struct sp_sink **self,
    struct sp_event *event)
{
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (event, struct sp_msgpipe, inevents [1]);
    sp_pipebase_in (&msgpipe->pipes [1]);
}

static void sp_msgpipe_outevent0 (const struct sp_sink **self,
    struct sp_event *event)
{
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (event, struct sp_msgpipe, outevents [0]);
    sp_pipebase_out (&msgpipe->pipes [0]);
}

static void sp_msgpipe_outevent1 (const struct sp_sink **self,
    struct sp_event *event)
{
    struct sp_msgpipe *msgpipe;

    msgpipe = sp_cont (event, struct sp_msgpipe, outevents [1]);
    sp_pipebase_out (&msgpipe->pipes [1]);
}

