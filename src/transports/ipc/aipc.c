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

#if !defined NN_HAVE_WINDOWS

#include "aipc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

#define NN_AIPC_STATE_IDLE 1
#define NN_AIPC_STATE_ACCEPTING 2
#define NN_AIPC_STATE_ACTIVE 3
#define NN_AIPC_STATE_STOPPING_SIPC 4
#define NN_AIPC_STATE_STOPPING_USOCK 5
#define NN_AIPC_STATE_DONE 6
#define NN_AIPC_STATE_STOPPING_SIPC_FINAL 7
#define NN_AIPC_STATE_STOPPING 8

/*  Private functions. */
static void nn_aipc_handler (struct nn_fsm *self, void *source, int type);

void nn_aipc_init (struct nn_aipc *self, struct nn_epbase *epbase,
    struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_aipc_handler, owner);
    self->state = NN_AIPC_STATE_IDLE;
    nn_usock_init (&self->usock, &self->fsm);
    self->listener = NULL;
    self->listener_owner = NULL;
    nn_sipc_init (&self->sipc, epbase, &self->fsm);
    nn_fsm_event_init (&self->accepted);
    nn_fsm_event_init (&self->done);
    nn_list_item_init (&self->item);
}

void nn_aipc_term (struct nn_aipc *self)
{
    nn_assert (self->state == NN_AIPC_STATE_IDLE);

    nn_list_item_term (&self->item);
    nn_fsm_event_term (&self->done);
    nn_fsm_event_term (&self->accepted);
    nn_sipc_term (&self->sipc);
    nn_usock_term (&self->usock);
    nn_fsm_term (&self->fsm);    
}

int nn_aipc_isidle (struct nn_aipc *self)
{
    return self->state == NN_AIPC_STATE_IDLE ? 1 : 0;
}

int nn_aipc_isstopped (struct nn_aipc *self)
{
    return self->state == NN_AIPC_STATE_IDLE ||
        self->state == NN_AIPC_STATE_STOPPING_SIPC_FINAL ||
        self->state == NN_AIPC_STATE_STOPPING ? 1 : 0;
}

void nn_aipc_start (struct nn_aipc *self, struct nn_usock *listener)
{
    nn_assert (self->state == NN_AIPC_STATE_IDLE);

    /*  Take ownership of the listener socket. */
    self->listener = listener;
    self->listener_owner = nn_usock_swap_owner (listener, &self->fsm);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_aipc_stop (struct nn_aipc *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_aipc_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_aipc *aipc;

    aipc = nn_cont (self, struct nn_aipc, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &aipc->fsm && type == NN_FSM_STOP)) {
        nn_assert (aipc->state != NN_AIPC_STATE_STOPPING &&
            aipc->state != NN_AIPC_STATE_STOPPING_SIPC_FINAL);
        if (!nn_sipc_isstopped (&aipc->sipc)) {
            nn_sipc_stop (&aipc->sipc);
            aipc->state = NN_AIPC_STATE_STOPPING_SIPC_FINAL;
            return;
        }
stop:
        if (!nn_usock_isstopped (&aipc->usock))
            nn_usock_stop (&aipc->usock);
        aipc->state = NN_AIPC_STATE_STOPPING;
    }
    if (nn_slow (aipc->state == NN_AIPC_STATE_STOPPING)) {
        if (nn_usock_isidle (&aipc->usock)) {
           if (aipc->listener) {
                nn_assert (aipc->listener_owner);
                nn_usock_swap_owner (aipc->listener, aipc->listener_owner);
                aipc->listener = NULL;
                aipc->listener_owner = NULL;
            }
            aipc->state = NN_AIPC_STATE_IDLE;
            nn_fsm_stopped (&aipc->fsm, aipc, NN_AIPC_STOPPED);
            return;
        }
        return;
    }
    if (nn_slow (aipc->state == NN_AIPC_STATE_STOPPING_SIPC_FINAL)) {
        if (source == &aipc->sipc && type == NN_SIPC_STOPPED)
            goto stop;
        return;
    }

    switch (aipc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_AIPC_STATE_IDLE:
        if (source == &aipc->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_usock_accept (aipc->listener, &aipc->usock);
                aipc->state = NN_AIPC_STATE_ACCEPTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACCEPTING state.                                                          */
/*  Waiting for incoming connection.                                          */
/******************************************************************************/
    case NN_AIPC_STATE_ACCEPTING:
        if (source == aipc->listener) {
            switch (type) {
            case NN_USOCK_ACCEPTED:

                /*  Return ownership of the listening socket to the parent. */
                nn_usock_swap_owner (aipc->listener, aipc->listener_owner);
                aipc->listener = NULL;
                aipc->listener_owner = NULL;
                nn_fsm_raise (&aipc->fsm, &aipc->accepted, aipc,
                    NN_AIPC_ACCEPTED);

                /*  Start the sipc state machine. */
                nn_sipc_start (&aipc->sipc, &aipc->usock);
                aipc->state = NN_AIPC_STATE_ACTIVE;

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_AIPC_STATE_ACTIVE:
        if (source == &aipc->sipc) {
            switch (type) {
            case NN_SIPC_ERROR:
                nn_sipc_stop (&aipc->sipc);
                aipc->state = NN_AIPC_STATE_STOPPING_SIPC;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_SIPC state.                                                      */
/******************************************************************************/
    case NN_AIPC_STATE_STOPPING_SIPC:
        if (source == &aipc->sipc) {
            switch (type) {
            case NN_SIPC_STOPPED:
                nn_usock_stop (&aipc->usock);
                aipc->state = NN_AIPC_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_USOCK state.                                                      */
/******************************************************************************/
    case NN_AIPC_STATE_STOPPING_USOCK:
        if (source == &aipc->usock) {
            switch (type) {
            case NN_USOCK_STOPPED:
                nn_fsm_raise (&aipc->fsm, &aipc->done, aipc, NN_AIPC_ERROR);
                aipc->state = NN_AIPC_STATE_DONE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

#endif

