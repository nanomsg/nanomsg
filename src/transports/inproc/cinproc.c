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

#include "cinproc.h"
#include "binproc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <stddef.h>

#define NN_CINPROC_STATE_IDLE 1
#define NN_CINPROC_STATE_DISCONNECTED 2
#define NN_CINPROC_STATE_ACTIVE 3

/*  Implementation of nn_epbase callback interface. */
static void nn_cinproc_stop (struct nn_epbase *self);
static void nn_cinproc_destroy (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_cinproc_vfptr = {
    nn_cinproc_stop,
    nn_cinproc_destroy
};

/*  Private functions. */
static void nn_cinproc_handler (struct nn_fsm *self, void *source, int type);

struct nn_cinproc *nn_cinproc_create (void *hint)
{
    struct nn_cinproc *self;

    self = nn_alloc (sizeof (struct nn_cinproc), "cinproc");
    alloc_assert (self);

    nn_epbase_init (&self->epbase, &nn_cinproc_vfptr, hint);
    nn_fsm_init_root (&self->fsm, nn_cinproc_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_CINPROC_STATE_IDLE;
    nn_sinproc_init (&self->sinproc, &self->epbase, &self->fsm);
    nn_list_item_init (&self->item);
    self->connects = 0;

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    return self;
}

static void nn_cinproc_stop (struct nn_epbase *self)
{
    struct nn_cinproc *cinproc;

    cinproc = nn_cont (self, struct nn_cinproc, epbase);

    nn_fsm_stop (&cinproc->fsm);
}

static void nn_cinproc_destroy (struct nn_epbase *self)
{
    struct nn_cinproc *cinproc;

    cinproc = nn_cont (self, struct nn_cinproc, epbase);

    nn_list_item_term (&cinproc->item);
    nn_sinproc_term (&cinproc->sinproc);
    nn_epbase_term (&cinproc->epbase);
    nn_fsm_term (&cinproc->fsm);

    nn_free (cinproc);
}

const char *nn_cinproc_getaddr (struct nn_cinproc *self)
{
    return nn_epbase_getaddr (&self->epbase);
}

void nn_cinproc_connect (struct nn_cinproc *self, struct nn_binproc *peer)
{
    nn_assert (self->state == NN_CINPROC_STATE_DISCONNECTED);
    nn_assert (0);
}

static void nn_cinproc_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_cinproc *cinproc;
    struct nn_sinproc *sinproc;

    cinproc = nn_cont (self, struct nn_cinproc, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &cinproc->fsm && type == NN_FSM_STOP))
        nn_assert (0);

    switch (cinproc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_CINPROC_STATE_IDLE:
        if (source == &cinproc->fsm) {
            switch (type) {
            case NN_FSM_START:
                cinproc->state = NN_CINPROC_STATE_DISCONNECTED;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  DISCONNECTED state.                                                       */
/******************************************************************************/
    case NN_CINPROC_STATE_DISCONNECTED:
        if (source == NULL) {
            switch (type) {
            default:
                nn_assert (0);
            }
        }

        /*  Here we assume that all other events are raised by the peer
            sinproc object. */
        sinproc = (struct nn_sinproc*) source;
        switch (type) {
        case NN_SINPROC_CONNECT:
printf ("%p CONNECT received (by cinproc)\n", sinproc);
            nn_sinproc_start_accept (&cinproc->sinproc, sinproc);
            cinproc->state = NN_CINPROC_STATE_ACTIVE;
            return;
        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

