/*
    Copyright (c) 2012-2014 Martin Sustrik  All rights reserved.

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

#include "atcpmux.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/attr.h"

#define NN_ATCPMUX_STATE_IDLE 1
#define NN_ATCPMUX_STATE_ACTIVE 2
#define NN_ATCPMUX_STATE_STOPPING_STCPMUX 3
#define NN_ATCPMUX_STATE_STOPPING_USOCK 4
#define NN_ATCPMUX_STATE_DONE 5
#define NN_ATCPMUX_STATE_STOPPING_STCPMUX_FINAL 6
#define NN_ATCPMUX_STATE_STOPPING 7

#define NN_ATCPMUX_SRC_USOCK 1
#define NN_ATCPMUX_SRC_STCPMUX 2

/*  Private functions. */
static void nn_atcpmux_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_atcpmux_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);

void nn_atcpmux_init (struct nn_atcpmux *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_atcpmux_handler, nn_atcpmux_shutdown,
        src, self, owner);
    self->state = NN_ATCPMUX_STATE_IDLE;
    self->epbase = epbase;
    nn_usock_init (&self->usock, NN_ATCPMUX_SRC_USOCK, &self->fsm);
    nn_stcpmux_init (&self->stcpmux, NN_ATCPMUX_SRC_STCPMUX,
        epbase, &self->fsm);
    nn_fsm_event_init (&self->accepted);
    nn_fsm_event_init (&self->done);
    nn_list_item_init (&self->item);
}

void nn_atcpmux_term (struct nn_atcpmux *self)
{
    nn_assert_state (self, NN_ATCPMUX_STATE_IDLE);

    nn_list_item_term (&self->item);
    nn_fsm_event_term (&self->done);
    nn_fsm_event_term (&self->accepted);
    nn_stcpmux_term (&self->stcpmux);
    nn_usock_term (&self->usock);
    nn_fsm_term (&self->fsm);
}

int nn_atcpmux_isidle (struct nn_atcpmux *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_atcpmux_start (struct nn_atcpmux *self, int fd)
{
    nn_assert_state (self, NN_ATCPMUX_STATE_IDLE);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Start the stcp state machine. */
    nn_usock_start_fd (&self->usock, fd);
    nn_stcpmux_start (&self->stcpmux, &self->usock);
    self->state = NN_ATCPMUX_STATE_ACTIVE;
}

void nn_atcpmux_stop (struct nn_atcpmux *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_atcpmux_shutdown (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_atcpmux *atcpmux;

    atcpmux = nn_cont (self, struct nn_atcpmux, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        if (!nn_stcpmux_isidle (&atcpmux->stcpmux)) {
            nn_epbase_stat_increment (atcpmux->epbase,
                NN_STAT_DROPPED_CONNECTIONS, 1);
            nn_stcpmux_stop (&atcpmux->stcpmux);
        }
        atcpmux->state = NN_ATCPMUX_STATE_STOPPING_STCPMUX_FINAL;
    }
    if (nn_slow (atcpmux->state == NN_ATCPMUX_STATE_STOPPING_STCPMUX_FINAL)) {
        if (!nn_stcpmux_isidle (&atcpmux->stcpmux))
            return;
        nn_usock_stop (&atcpmux->usock);
        atcpmux->state = NN_ATCPMUX_STATE_STOPPING;
    }
    if (nn_slow (atcpmux->state == NN_ATCPMUX_STATE_STOPPING)) {
        if (!nn_usock_isidle (&atcpmux->usock))
            return;
        atcpmux->state = NN_ATCPMUX_STATE_IDLE;
        nn_fsm_stopped (&atcpmux->fsm, NN_ATCPMUX_STOPPED);
        return;
    }

    nn_fsm_bad_action(atcpmux->state, src, type);
}

static void nn_atcpmux_handler (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_atcpmux *atcpmux;

    atcpmux = nn_cont (self, struct nn_atcpmux, fsm);

    switch (atcpmux->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_ATCPMUX_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                // TODO
                atcpmux->state = NN_ATCPMUX_STATE_ACTIVE;
                return;
            default:
                nn_fsm_bad_action (atcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (atcpmux->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_ATCPMUX_STATE_ACTIVE:
        switch (src) {

        case NN_ATCPMUX_SRC_STCPMUX:
            switch (type) {
            case NN_STCPMUX_ERROR:
                nn_stcpmux_stop (&atcpmux->stcpmux);
                atcpmux->state = NN_ATCPMUX_STATE_STOPPING_STCPMUX;
                nn_epbase_stat_increment (atcpmux->epbase,
                    NN_STAT_BROKEN_CONNECTIONS, 1);
                return;
            default:
                nn_fsm_bad_action (atcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (atcpmux->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_STCPMUX state.                                                   */
/******************************************************************************/
    case NN_ATCPMUX_STATE_STOPPING_STCPMUX:
        switch (src) {

        case NN_ATCPMUX_SRC_STCPMUX:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_STCPMUX_STOPPED:
                nn_usock_stop (&atcpmux->usock);
                atcpmux->state = NN_ATCPMUX_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (atcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (atcpmux->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_USOCK state.                                                      */
/******************************************************************************/
    case NN_ATCPMUX_STATE_STOPPING_USOCK:
        switch (src) {

        case NN_ATCPMUX_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_USOCK_STOPPED:
                nn_fsm_raise (&atcpmux->fsm, &atcpmux->done, NN_ATCPMUX_ERROR);
                atcpmux->state = NN_ATCPMUX_STATE_DONE;
                return;
            default:
                nn_fsm_bad_action (atcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (atcpmux->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (atcpmux->state, src, type);
    }
}

