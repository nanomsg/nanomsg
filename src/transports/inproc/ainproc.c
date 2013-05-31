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

#include "ainproc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

#include <stddef.h>

#define NN_AINPROC_STATE_IDLE 1
#define NN_AINPROC_STATE_CONNECTING 2
#define NN_AINPROC_STATE_ACTIVE 2
#define NN_AINPROC_STATE_STOPPING 3

/*  Private functions. */
static void nn_ainproc_handler (struct nn_fsm *self, void *source, int type);

void nn_ainproc_init (struct nn_ainproc *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_ainproc_handler, owner);
    self->state = NN_AINPROC_STATE_IDLE;
    nn_msgpipe_init (&self->local, &self->fsm);
    self->remote = NULL;
    nn_list_item_init (&self->item);
}

void nn_ainproc_term (struct nn_ainproc *self)
{
    nn_list_item_term (&self->item);
    nn_msgpipe_term (&self->local);
    nn_fsm_term (&self->fsm);
}

void nn_ainproc_start (struct nn_ainproc *self)
{
    nn_fsm_start (&self->fsm);
}

void nn_ainproc_stop (struct nn_ainproc *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_ainproc_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_ainproc *ainproc;

    ainproc = nn_cont (self, struct nn_ainproc, fsm);

    nn_assert (0);
}

