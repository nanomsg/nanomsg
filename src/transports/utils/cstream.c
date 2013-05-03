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

#include "cstream.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"

#include <string.h>

#define NN_CSTREAM_STATE_INIT 1
#define NN_CSTREAM_STATE_WAITING 2
#define NN_CSTREAM_STATE_FINISHING_WAITING 3
#define NN_CSTREAM_STATE_CONNECTING 4
#define NN_CSTREAM_STATE_CONNECTED 5
#define NN_CSTREAM_STATE_CLOSING_TIMER 6
#define NN_CSTREAM_STATE_CLOSING_STREAM 7
#define NN_CSTREAM_STATE_CLOSING_USOCK 8
#define NN_CSTREAM_STATE_CLOSED 9

#define NN_CSTREAM_EVENT_START 1
#define NN_CSTREAM_EVENT_CLOSE 2

/*  Private functions. */
static void nn_cstream_term (struct nn_cstream *self);
static void nn_cstream_callback (struct nn_fsm *fsm, void *source, int type);
static int nn_cstream_compute_retry_ivl (struct nn_cstream *self);

/*  Implementation of nn_epbase virtual interface. */
static void nn_cstream_close (struct nn_epbase *self);
static void nn_cstream_destroy (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_cstream_epbase_vfptr = {
    nn_cstream_close,
    nn_cstream_destroy
};

int nn_cstream_create (const struct nn_cstream_vfptr *vfptr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_cstream *self;

    self = nn_alloc (sizeof (struct nn_cstream), "cstream (ipc)");
    alloc_assert (self);

    self->vfptr = vfptr;

    /*  Initialise the 'endpoint' base class. */
    nn_epbase_init (&self->epbase, &nn_cstream_epbase_vfptr, hint);

    /*  Initialise the state machine. */
    nn_fsm_init_root (&self->fsm, nn_cstream_callback,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_CSTREAM_STATE_INIT;

    /*  TODO: Check the syntax of the address and return error if it is
        not a valid address string. Don't do any blocking DNS operations
        though! */

    /*  Initialise the child state machine. */
    nn_stream_init (&self->stream, &self->epbase, &self->fsm);

    /*  Initialise the retry timer. */
    self->retry_ivl = -1;
    nn_timer_init (&self->retry_timer, &self->fsm);

    /*  Start the state machine. */
    nn_cstream_callback (&self->fsm, NULL, NN_CSTREAM_EVENT_START);

    *epbase = &self->epbase;
    return 0;
}

static void nn_cstream_term (struct nn_cstream *self)
{
    nn_assert (self->state == NN_CSTREAM_STATE_CLOSED);

    nn_timer_term (&self->retry_timer);
    nn_stream_term (&self->stream);
    nn_usock_term (&self->usock);
    nn_epbase_term (&self->epbase);
}

static void nn_cstream_callback (struct nn_fsm *fsm, void *source, int type)
{
    int rc;
    struct nn_cstream *cstream;
    struct sockaddr_storage local;
    socklen_t locallen;
    struct sockaddr_storage remote;
    socklen_t remotelen;
    int sndbuf;
    int rcvbuf;
    size_t sz;

    cstream = nn_cont (fsm, struct nn_cstream, fsm);

    switch (cstream->state) {

/******************************************************************************/
/*  INIT state.                                                               */
/******************************************************************************/
    case NN_CSTREAM_STATE_INIT:
        if (source == NULL) {
            switch (type) {
            case NN_CSTREAM_EVENT_START:

                /*  Open the socket. */
                rc = cstream->vfptr->open (&cstream->usock, &cstream->fsm);
                errnum_assert (rc == 0, -rc);

                /*  Apply current values of NN_SNDBUF and NN_RCVBUF options. */    
                sz = sizeof (sndbuf);
                nn_epbase_getopt (&cstream->epbase,
                    NN_SOL_SOCKET, NN_SNDBUF, &sndbuf, &sz);
                nn_assert (sz == sizeof (sndbuf));
                rc = nn_usock_setsockopt (&cstream->usock,
                    SOL_SOCKET, SO_SNDBUF, &sndbuf, sz);
                errnum_assert (rc == 0, -rc);
                sz = sizeof (rcvbuf);
                nn_epbase_getopt (&cstream->epbase,
                    NN_SOL_SOCKET, NN_RCVBUF, &rcvbuf, &sz);
                nn_assert (sz == sizeof (rcvbuf));
                rc = nn_usock_setsockopt (&cstream->usock,
                    SOL_SOCKET, SO_RCVBUF, &rcvbuf, sz);
                errnum_assert (rc == 0, -rc);

                /* Try to resolve the address. */
                rc = cstream->vfptr->resolve (
                    nn_epbase_getaddr (&cstream->epbase),
                    &local, &locallen, &remote, &remotelen);

                /* If the address resolution have failed, wait before
                   re-trying. */
                if (rc < 0) {
                    nn_timer_start (&cstream->retry_timer,
                        nn_cstream_compute_retry_ivl (cstream));
                    cstream->state = NN_CSTREAM_STATE_WAITING;
                    return;
                }

                /*  Start connecting. */
                if (rc & NN_CSTREAM_DOBIND)
                    nn_usock_bind (&cstream->usock,
                        (struct sockaddr*) &local, locallen);
                nn_usock_connect (&cstream->usock,
                    (struct sockaddr*) &remote, remotelen);
                cstream->state = NN_CSTREAM_STATE_CONNECTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  WAITING state.                                                            */
/******************************************************************************/
    case NN_CSTREAM_STATE_WAITING:
        if (source == &cstream->retry_timer) {
            switch (type) {
            case NN_TIMER_TIMEOUT:
                
                /*  Timer expired. Let's start connecting once more. */
                cstream->state = NN_CSTREAM_STATE_INIT;
                nn_cstream_callback (&cstream->fsm, NULL,
                    NN_CSTREAM_EVENT_START);
                return;

            default:
                nn_assert (0);
            }
        }
        if (source == NULL) {
            switch (type) {
            case NN_CSTREAM_EVENT_CLOSE:

                /*  User is closing the object while waiting for reconnection.
                    Cancel the timer and proceed with shutdown. */
                nn_timer_close (&cstream->retry_timer);
                cstream->state = NN_CSTREAM_STATE_CLOSING_TIMER;

                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  FINISHING_WAITING state.                                                  */
/******************************************************************************/
    case NN_CSTREAM_STATE_FINISHING_WAITING:
        nn_assert (0);

/******************************************************************************/
/*  CONNECTING state.                                                         */
/******************************************************************************/
    case NN_CSTREAM_STATE_CONNECTING:
        if (source == &cstream->usock) {
            switch (type) {
            case NN_USOCK_CONNECTED:

                /*  Pass the control to the embedded 'stream' state machine. */
                nn_stream_start (&cstream->stream, &cstream->usock);
                cstream->state = NN_CSTREAM_STATE_CONNECTED;
                return;

            case NN_USOCK_ERROR:

                /* Connecting failed. Wait a while before re-connecting. */
                nn_timer_start (&cstream->retry_timer,
                    nn_cstream_compute_retry_ivl (cstream));
                cstream->state = NN_CSTREAM_STATE_WAITING;

                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTED state.                                                          */
/******************************************************************************/
    case NN_CSTREAM_STATE_CONNECTED:
        if (source == &cstream->usock) {
            switch (type) {
            case NN_STREAM_ERROR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        if (source == NULL) {
            switch (type) {
            case NN_CSTREAM_EVENT_CLOSE:
                nn_stream_close (&cstream->stream);
                cstream->state = NN_CSTREAM_STATE_CLOSING_STREAM;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_TIMER state.                                                      */
/******************************************************************************/
    case NN_CSTREAM_STATE_CLOSING_TIMER:
        if (source == &cstream->retry_timer) {
            switch (type) {
            case NN_TIMER_CLOSED:
                nn_usock_close (&cstream->usock);
                cstream->state = NN_CSTREAM_STATE_CLOSING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_STREAM state.                                                     */
/******************************************************************************/
    case NN_CSTREAM_STATE_CLOSING_STREAM:
        if (source == &cstream->stream) {
            switch (type) {
            case NN_STREAM_CLOSED:
                nn_usock_close (&cstream->usock);
                cstream->state = NN_CSTREAM_STATE_CLOSING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_USOCK state.                                                      */
/******************************************************************************/
    case NN_CSTREAM_STATE_CLOSING_USOCK:
        if (source == &cstream->usock) {
            switch (type) {
            case NN_USOCK_CLOSED:
                cstream->state = NN_CSTREAM_STATE_CLOSED;
                nn_epbase_closed (&cstream->epbase);
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSED state.                                                             */
/******************************************************************************/
    case NN_CSTREAM_STATE_CLOSED:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

static void nn_cstream_close (struct nn_epbase *self)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, epbase);

    /*  Pass the event to the state machine. */
    nn_cstream_callback (&cstream->fsm, NULL, NN_CSTREAM_EVENT_CLOSE);
}

static void nn_cstream_destroy (struct nn_epbase *self)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, epbase);

    nn_cstream_term (cstream);
    nn_free (cstream);
}

static int nn_cstream_compute_retry_ivl (struct nn_cstream *self)
{
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;
    int result;
    unsigned int random;

    /*  Get relevant options' values. */
    sz = sizeof (reconnect_ivl);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL,
        &reconnect_ivl, &sz);
    nn_assert (sz == sizeof (reconnect_ivl));
    sz = sizeof (reconnect_ivl_max);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL_MAX,
        &reconnect_ivl_max, &sz);
    nn_assert (sz == sizeof (reconnect_ivl_max));

    /*  Negative number means that reconnect sequence is starting.
        The reconnect interval in this case is NN_RECONNECT_IVL. */
    if (self->retry_ivl < 0)
        self->retry_ivl = reconnect_ivl;

    /*  Current retry_ivl will be returned to the caller. */
    result = self->retry_ivl;

    /*  Re-compute new retry interval. */
    if (reconnect_ivl_max > 0 && reconnect_ivl_max > reconnect_ivl) {
        self->retry_ivl *= 2;
        if (self->retry_ivl > reconnect_ivl_max)
            self->retry_ivl = reconnect_ivl_max;
    }

    /*  Randomise the result to prevent re-connection storms when network
        and/or server goes down and then up again. This may rise
        the reconnection interval at most twice and at most by one second. */
    nn_random_generate (&random, sizeof (random));
    result += (random % result % 1000);
    return result;
}

