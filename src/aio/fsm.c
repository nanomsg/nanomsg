/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "fsm.h"
#include "ctx.h"

#include "../utils/err.h"

#include <stddef.h>

#define NN_FSM_STATE_IDLE 1
#define NN_FSM_STATE_ACTIVE 2
#define NN_FSM_STATE_STOPPING 3

void nn_fsm_event_init (struct nn_fsm_event *self)
{
    self->fsm = NULL;
    self->source = NULL;
    self->type = -1;
    nn_queue_item_init (&self->item);
}

void nn_fsm_event_term (struct nn_fsm_event *self)
{
    nn_queue_item_term (&self->item);
}

int nn_fsm_event_active (struct nn_fsm_event *self)
{
    return nn_queue_item_isinqueue (&self->item);
}

void nn_fsm_event_process (struct nn_fsm_event *self)
{
    int type;

    type = self->type;
    self->type = -1;
    self->fsm->fn (self->fsm, self->source, type);
}

void nn_fsm_init_root (struct nn_fsm *self, nn_fsm_fn fn, struct nn_ctx *ctx)
{
    self->fn = fn;
    self->state = NN_FSM_STATE_IDLE;
    self->owner = NULL;
    self->ctx = ctx;
    nn_fsm_event_init (&self->stopped);
}

void nn_fsm_init (struct nn_fsm *self, nn_fsm_fn fn, struct nn_fsm *owner)
{
    self->fn = fn;
    self->state = NN_FSM_STATE_IDLE;
    self->owner = owner;
    self->ctx = owner->ctx;
    nn_fsm_event_init (&self->stopped);
}

void nn_fsm_term (struct nn_fsm *self)
{
    nn_assert (nn_fsm_isidle (self));
    nn_fsm_event_term (&self->stopped);
}

void nn_fsm_start (struct nn_fsm *self)
{
    nn_assert (nn_fsm_isidle (self));
    self->fn (self, self, NN_FSM_START);
    self->state = NN_FSM_STATE_ACTIVE;
}

int nn_fsm_isidle (struct nn_fsm *self)
{
    return self->state == NN_FSM_STATE_IDLE &&
        !nn_fsm_event_active (&self->stopped) ? 1 : 0;
}

void nn_fsm_stop (struct nn_fsm *self)
{
    /*  If stopping of the state machine was already requested, do nothing. */
    if (self->state == NN_FSM_STATE_IDLE)
        return;

    self->state = NN_FSM_STATE_STOPPING;
    self->fn (self, self, NN_FSM_STOP);
}

void nn_fsm_stopped (struct nn_fsm *self, void *source, int type)
{
    nn_assert (self->state == NN_FSM_STATE_STOPPING);
    nn_fsm_raise (self, &self->stopped, source, type);
    self->state = NN_FSM_STATE_IDLE;
}

void nn_fsm_stopped_noevent (struct nn_fsm *self)
{
    nn_assert (self->state == NN_FSM_STATE_STOPPING);
    self->state = NN_FSM_STATE_IDLE;
}

struct nn_fsm *nn_fsm_swap_owner (struct nn_fsm *self, struct nn_fsm *newowner)
{
    struct nn_fsm *oldowner;

    oldowner = self->owner;
    self->owner = newowner;

    return oldowner;
}

struct nn_worker *nn_fsm_choose_worker (struct nn_fsm *self)
{
    return nn_ctx_choose_worker (self->ctx);
}

void nn_fsm_raise (struct nn_fsm *self, struct nn_fsm_event *event,
    void *source, int type)
{    
    event->fsm = self->owner;
    event->source = source;
    event->type = type;
    nn_ctx_raise (self->ctx, event);
}

