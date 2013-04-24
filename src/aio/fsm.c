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

#include <stddef.h>

void nn_fsm_event_init (struct nn_fsm_event *self, void *source, int type)
{
    self->fsm = NULL;
    self->source = source;
    self->type = type;
    nn_queue_item_init (&self->item);
}

void nn_fsm_event_term (struct nn_fsm_event *self)
{
    nn_queue_item_term (&self->item);
}

void nn_fsm_init_root (struct nn_fsm *self, nn_fsm_fn fn, struct nn_ctx *ctx)
{
    self->fn = fn;
    self->owner = NULL;
    self->ctx = ctx;
}

void nn_fsm_init (struct nn_fsm *self, nn_fsm_fn fn, struct nn_fsm *owner)
{
    self->fn = fn;
    self->owner = owner;
    self->ctx = owner->ctx;
}

void nn_fsm_term (struct nn_fsm *self)
{
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

void nn_fsm_raise (struct nn_fsm *self, struct nn_fsm_event *event)
{
    event->fsm = self->owner;
    nn_ctx_raise (self->ctx, event);
}

