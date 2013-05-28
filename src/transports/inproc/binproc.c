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

#include "binproc.h"
#include "msgpipe.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"

#define NN_BINPROC_STATE_IDLE 1
#define NN_BINPROC_STATE_ACTIVE 2
#define NN_BINPROC_STATE_STOPPING 3

/*  Implementation of nn_epbase interface. */
static void nn_binproc_stop (struct nn_epbase *self);
static void nn_binproc_destroy (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_binproc_vfptr = {
    nn_binproc_stop,
    nn_binproc_destroy
};

/*  Private functions. */
static void nn_binproc_handler (struct nn_fsm *self, void *source, int type);

struct nn_binproc *nn_binproc_create (void *hint)
{
    struct nn_binproc *self;

    self = nn_alloc (sizeof (struct nn_binproc), "binproc");
    alloc_assert (self);

    nn_epbase_init (&self->epbase, &nn_binproc_vfptr, hint);
    nn_fsm_init_root (&self->fsm, nn_binproc_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_BINPROC_STATE_IDLE;
    nn_list_init (&self->msgpipes);
    nn_list_item_init (&self->item);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    return self;
}

static void nn_binproc_stop (struct nn_epbase *self)
{
    struct nn_binproc *binproc;

    binproc = nn_cont (self, struct nn_binproc, epbase);

    nn_fsm_stop (&binproc->fsm);
}

static void nn_binproc_destroy (struct nn_epbase *self)
{
    struct nn_binproc *binproc;

    binproc = nn_cont (self, struct nn_binproc, epbase);

    nn_list_item_term (&binproc->item);
    nn_list_term (&binproc->msgpipes);
    nn_fsm_term (&binproc->fsm);
    nn_epbase_term (&binproc->epbase);

    nn_free (binproc);
}

const char *nn_binproc_getaddr (struct nn_binproc *self)
{
    return nn_epbase_getaddr (&self->epbase);
}

struct nn_msgpipe *nn_binproc_connect (struct nn_binproc *self,
    struct nn_msgpipe *peer)
{
    nn_assert (0);
}

static void nn_binproc_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_binproc *binproc;

    binproc = nn_cont (self, struct nn_binproc, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &binproc->fsm && type == NN_FSM_STOP))
        nn_assert (0);
    if (nn_slow (binproc->state == NN_BINPROC_STATE_STOPPING))
        nn_assert (0);

    switch (binproc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_BINPROC_STATE_IDLE:
        if (source == &binproc->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_BINPROC_STATE_ACTIVE:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

