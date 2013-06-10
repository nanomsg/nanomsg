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

#include "atcp.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

#define NN_ATCP_STATE_IDLE 1
#define NN_ATCP_STATE_ACCEPTING 2
#define NN_ATCP_STATE_ACTIVE 3
#define NN_ATCP_STATE_STOPPING_STCP 4
#define NN_ATCP_STATE_STOPPING_USOCK 5
#define NN_ATCP_STATE_DONE 6
#define NN_ATCP_STATE_STOPPING_STCP_FINAL 7
#define NN_ATCP_STATE_STOPPING 8

/*  Private functions. */
static void nn_atcp_handler (struct nn_fsm *self, void *source, int type);

void nn_atcp_init (struct nn_atcp *self, struct nn_epbase *epbase,
    struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_atcp_handler, owner);
    self->state = NN_ATCP_STATE_IDLE;
    nn_usock_init (&self->usock, &self->fsm);
    self->listener = NULL;
    self->listener_owner = NULL;
    nn_stcp_init (&self->stcp, epbase, &self->fsm);
    nn_fsm_event_init (&self->accepted);
    nn_fsm_event_init (&self->done);
    nn_list_item_init (&self->item);
}

void nn_atcp_term (struct nn_atcp *self)
{
    nn_assert (self->state == NN_ATCP_STATE_IDLE);

    nn_list_item_term (&self->item);
    nn_fsm_event_term (&self->done);
    nn_fsm_event_term (&self->accepted);
    nn_stcp_term (&self->stcp);
    nn_usock_term (&self->usock);
    nn_fsm_term (&self->fsm);    
}

int nn_atcp_isidle (struct nn_atcp *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_atcp_start (struct nn_atcp *self, struct nn_usock *listener)
{
    nn_assert (self->state == NN_ATCP_STATE_IDLE);

    /*  Take ownership of the listener socket. */
    self->listener = listener;
    self->listener_owner = nn_usock_swap_owner (listener, &self->fsm);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_atcp_stop (struct nn_atcp *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_atcp_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_atcp *atcp;

    atcp = nn_cont (self, struct nn_atcp, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &atcp->fsm && type == NN_FSM_STOP)) {
        nn_stcp_stop (&atcp->stcp);
        atcp->state = NN_ATCP_STATE_STOPPING_STCP_FINAL;
    }
    if (nn_slow (atcp->state == NN_ATCP_STATE_STOPPING_STCP_FINAL)) {
        if (!nn_stcp_isidle (&atcp->stcp))
            return;
        nn_usock_stop (&atcp->usock);
        atcp->state = NN_ATCP_STATE_STOPPING;
    }
    if (nn_slow (atcp->state == NN_ATCP_STATE_STOPPING)) {
        if (!nn_usock_isidle (&atcp->usock))
            return;
       if (atcp->listener) {
            nn_assert (atcp->listener_owner);
            nn_usock_swap_owner (atcp->listener, atcp->listener_owner);
            atcp->listener = NULL;
            atcp->listener_owner = NULL;
        }
        atcp->state = NN_ATCP_STATE_IDLE;
        nn_fsm_stopped (&atcp->fsm, atcp, NN_ATCP_STOPPED);
        return;
    }

    switch (atcp->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_ATCP_STATE_IDLE:
        if (source == &atcp->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_usock_accept (&atcp->usock, atcp->listener);
                atcp->state = NN_ATCP_STATE_ACCEPTING;
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
    case NN_ATCP_STATE_ACCEPTING:
        if (source == &atcp->usock) {
            switch (type) {
            case NN_USOCK_ACCEPTED:

                /*  Return ownership of the listening socket to the parent. */
                nn_usock_swap_owner (atcp->listener, atcp->listener_owner);
                atcp->listener = NULL;
                atcp->listener_owner = NULL;
                nn_fsm_raise (&atcp->fsm, &atcp->accepted, atcp,
                    NN_ATCP_ACCEPTED);

                /*  Start the stcp state machine. */
                nn_usock_activate (&atcp->usock);
                nn_stcp_start (&atcp->stcp, &atcp->usock);
                atcp->state = NN_ATCP_STATE_ACTIVE;

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_ATCP_STATE_ACTIVE:
        if (source == &atcp->stcp) {
            switch (type) {
            case NN_STCP_ERROR:
                nn_stcp_stop (&atcp->stcp);
                atcp->state = NN_ATCP_STATE_STOPPING_STCP;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_STCP state.                                                      */
/******************************************************************************/
    case NN_ATCP_STATE_STOPPING_STCP:
        if (source == &atcp->stcp) {
            switch (type) {
            case NN_STCP_STOPPED:
                nn_usock_stop (&atcp->usock);
                atcp->state = NN_ATCP_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_USOCK state.                                                      */
/******************************************************************************/
    case NN_ATCP_STATE_STOPPING_USOCK:
        if (source == &atcp->usock) {
            switch (type) {
            case NN_USOCK_STOPPED:
                nn_fsm_raise (&atcp->fsm, &atcp->done, atcp, NN_ATCP_ERROR);
                atcp->state = NN_ATCP_STATE_DONE;
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

