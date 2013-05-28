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
#include "msgqueue.h"

#include "../../utils/err.h"

#define NN_MSGPIPE_STATE_IDLE 1

/*  Private functions. */
static void nn_msgpipe_handler (struct nn_fsm *self, void *source, int type);

void nn_msgpipe_init (struct nn_msgpipe *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_msgpipe_handler, owner);
    self->state = NN_MSGPIPE_STATE_IDLE;
    nn_msgqueue_init (&self->in, 1000000);
}

void nn_msgpipe_term (struct nn_msgpipe *self)
{
    nn_msgqueue_term (&self->in);
    nn_fsm_term (&self->fsm);
}

int nn_msgpipe_isidle (struct nn_msgpipe *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_msgpipe_start (struct nn_msgpipe *self)
{
    nn_fsm_start (&self->fsm);
}

void nn_msgpipe_stop (struct nn_msgpipe *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_msgpipe_handler (struct nn_fsm *self, void *source, int type)
{
    nn_assert (0);
}

