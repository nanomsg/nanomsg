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

#if !defined NN_HAVE_WINDOWS

#include "streamhdr.h"

#include "../../aio/timer.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/wire.h"

#include <string.h>

#define NN_STREAMHDR_STATE_IDLE 1
#define NN_STREAMHDR_STATE_SENDING 2
#define NN_STREAMHDR_STATE_RECEIVING 3
#define NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR 4
#define NN_STREAMHDR_STATE_STOPPING_TIMER_DONE 5
#define NN_STREAMHDR_STATE_DONE 6
#define NN_STREAMHDR_STATE_STOPPING 7

/*  Private functions. */
static void nn_streamhdr_handler (struct nn_fsm *self, void *source, int type);

void nn_streamhdr_init (struct nn_streamhdr *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_streamhdr_handler, owner);
    self->state = NN_STREAMHDR_STATE_IDLE;
    nn_timer_init (&self->timer, &self->fsm);
    nn_fsm_event_init (&self->done);

    /*  TODO: Prepare the outgoing protocol header.  */
#if 0
    sz = sizeof (protocol);
    nn_epbase_getopt (epbase, NN_SOL_SOCKET, NN_PROTOCOL, &protocol, &sz);
    errnum_assert (rc == 0, -rc);
    nn_assert (sz == sizeof (protocol));
#endif
    memcpy (self->protohdr, "\0\0SP\0\0\0\0", 8);
#if 0
    nn_puts (self->protohdr + 4, (uint16_t) protocol);
#endif

    self->usock = NULL;
    self->usock_owner = NULL;
}

void nn_streamhdr_term (struct nn_streamhdr *self)
{
    nn_assert (self->state == NN_STREAMHDR_STATE_IDLE);

    nn_fsm_event_term (&self->done);
    nn_timer_term (&self->timer);
    nn_fsm_term (&self->fsm);
}

int nn_streamhdr_isidle (struct nn_streamhdr *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_streamhdr_start (struct nn_streamhdr *self, struct nn_usock *usock)
{
    /*  Take ownership of the underlying socket. */
    nn_assert (self->usock == NULL && self->usock_owner == NULL);
    self->usock_owner = nn_usock_swap_owner (usock, &self->fsm);
    self->usock = usock;

    /*  Launch the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_streamhdr_stop (struct nn_streamhdr *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_streamhdr_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_streamhdr *streamhdr;
    struct nn_iovec iovec;

    streamhdr = nn_cont (self, struct nn_streamhdr, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &streamhdr->fsm && type == NN_FSM_STOP)) {
        nn_timer_stop (&streamhdr->timer);
        streamhdr->state = NN_STREAMHDR_STATE_STOPPING;
    }
    if (nn_slow (streamhdr->state == NN_STREAMHDR_STATE_STOPPING)) {
        if (!nn_timer_isidle (&streamhdr->timer))
            return;
        streamhdr->state = NN_STREAMHDR_STATE_IDLE;
        nn_fsm_stopped (&streamhdr->fsm, streamhdr, NN_STREAMHDR_STOPPED);
        return;
    }

    switch (streamhdr->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_STREAMHDR_STATE_IDLE:
        if (source == &streamhdr->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_timer_start (&streamhdr->timer, 1000);
                iovec.iov_base = streamhdr->protohdr;
                iovec.iov_len = sizeof (streamhdr->protohdr);
                nn_usock_send (streamhdr->usock, &iovec, 1);
                streamhdr->state = NN_STREAMHDR_STATE_SENDING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  SENDING state.                                                            */
/******************************************************************************/
    case NN_STREAMHDR_STATE_SENDING:
        if (source == streamhdr->usock) {
            switch (type) {
            case NN_USOCK_SENT:
                nn_usock_recv (streamhdr->usock, streamhdr->protohdr,
                    sizeof (streamhdr->protohdr));
                streamhdr->state = NN_STREAMHDR_STATE_RECEIVING;
                return;
            case NN_USOCK_ERROR:
                nn_timer_stop (&streamhdr->timer);
                streamhdr->state = NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR;
                return;
            default:
                nn_assert (0);
            }
        }
        if (source == &streamhdr->timer) {
            switch (type) {
            case NN_TIMER_TIMEOUT:
                nn_timer_stop (&streamhdr->timer);
                streamhdr->state = NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  RECEIVING state.                                                          */
/******************************************************************************/
    case NN_STREAMHDR_STATE_RECEIVING:
        if (source == streamhdr->usock) {
            switch (type) {
            case NN_USOCK_RECEIVED:

                /*  TODO: Check the header here. */

                nn_timer_stop (&streamhdr->timer);
                streamhdr->state = NN_STREAMHDR_STATE_STOPPING_TIMER_DONE;
                return;
            case NN_USOCK_ERROR:
                nn_timer_stop (&streamhdr->timer);
                streamhdr->state = NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR;
                return;
            default:
                nn_assert (0);
            }
        }
        if (source == &streamhdr->timer) {
            switch (type) {
            case NN_TIMER_TIMEOUT:
                nn_timer_stop (&streamhdr->timer);
                streamhdr->state = NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_TIMER_ERROR state.                                               */
/******************************************************************************/
    case NN_STREAMHDR_STATE_STOPPING_TIMER_ERROR:
        if (source == &streamhdr->timer) {
            switch (type) {
            case NN_TIMER_STOPPED:
                nn_usock_swap_owner (streamhdr->usock, streamhdr->usock_owner);
                streamhdr->usock = NULL;
                streamhdr->usock_owner = NULL;
                streamhdr->state = NN_STREAMHDR_STATE_DONE;
                nn_fsm_raise (&streamhdr->fsm, &streamhdr->done, streamhdr,
                    NN_STREAMHDR_ERROR);
                return;
            default:
                nn_assert (0);
            }
        }        
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_TIMER_DONE state.                                                */
/******************************************************************************/
    case NN_STREAMHDR_STATE_STOPPING_TIMER_DONE:
        if (source == &streamhdr->timer) {
            switch (type) {
            case NN_TIMER_STOPPED:
                nn_usock_swap_owner (streamhdr->usock, streamhdr->usock_owner);
                streamhdr->usock = NULL;
                streamhdr->usock_owner = NULL;
                streamhdr->state = NN_STREAMHDR_STATE_DONE;
                nn_fsm_raise (&streamhdr->fsm, &streamhdr->done, streamhdr,
                    NN_STREAMHDR_OK);
                return;
            default:
                nn_assert (0);
            }
        }        
        nn_assert (0);

/******************************************************************************/
/*  DONE state.                                                               */
/*  The header exchange was either done successfully of failed. There's       */
/*  nothing that can be done in this state except stopping the object.        */
/******************************************************************************/
    case NN_STREAMHDR_STATE_DONE:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

#endif

