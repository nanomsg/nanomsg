/*
    Copyright (c) 2012 250bpm s.r.o.

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
#include "err.h"
#include "cont.h"
#include "addr.h"
#include "alloc.h"
#include "random.h"

#include <string.h>

/*  States. */
static const struct nn_cp_sink nn_cstream_state_waiting;
static const struct nn_cp_sink nn_cstream_state_connecting;
static const struct nn_cp_sink nn_cstream_state_connected;
static const struct nn_cp_sink nn_cstream_state_closing;

/*  Private functions. */
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

/*  Implementation of nn_epbase interface. */
static int nn_cstream_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_cstream_epbase_vfptr =
    {nn_cstream_close};

/******************************************************************************/
/*  State: WAITING                                                            */
/******************************************************************************/

static void nn_cstream_waiting_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer);
static const struct nn_cp_sink nn_cstream_state_waiting = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_cstream_waiting_timeout,
    NULL
};

int nn_cstream_init (struct nn_cstream *self, const char *addr, void *hint,
    int (*initsockfn) (struct nn_usock *sock, int sndbuf, int rcvbuf,
    struct nn_cp *cp), int (*resolvefn) (const char *addr,
    struct sockaddr_storage *local, socklen_t *locallen,
    struct sockaddr_storage *remote, socklen_t *remotelen))
{
    int rc;
    int sndbuf;
    int rcvbuf;
    size_t sz;

    self->initsockfn = initsockfn;
    self->resolvefn = resolvefn;

    /*  TODO: Check the syntax of the address and return error if it is
        not a valid address string. Don't do any blocking DNS operations
        though! */

    /*  Initialise the base class. */
    nn_epbase_init (&self->epbase, &nn_cstream_epbase_vfptr, addr, hint);

    /*  Get the current values of NN_SNDBUF and NN_RCVBUF options. */    
    sz = sizeof (sndbuf);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_SNDBUF, &sndbuf, &sz);
    nn_assert (sz == sizeof (sndbuf));
    sz = sizeof (rcvbuf);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RCVBUF, &rcvbuf, &sz);
    nn_assert (sz == sizeof (rcvbuf));

    /*  Open a socket. */
    rc = self->initsockfn (&self->usock, sndbuf, rcvbuf,
        nn_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    nn_usock_setsink (&self->usock, &self->sink);

    /*  Initialise the retry timer. */
    self->retry_ivl = -1;
    nn_timer_init (&self->retry_timer, &self->sink,
        nn_epbase_getcp (&self->epbase));

    /*  Pretend we were waiting for the re-connect timer and that the timer
        have expired. */
    self->sink = &nn_cstream_state_waiting;
    nn_cstream_waiting_timeout (&self->sink, &self->retry_timer);

    return 0;
}

static void nn_cstream_waiting_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer)
{
    int rc;
    struct nn_cstream *cstream;
    struct sockaddr_storage local;
    socklen_t locallen;
    struct sockaddr_storage remote;
    socklen_t remotelen;

    cstream = nn_cont (self, struct nn_cstream, sink);

    /*  Retry timer expired. Now we'll try to resolve the address. */
    rc = cstream->resolvefn (nn_epbase_getaddr (&cstream->epbase),
        &local, &locallen, &remote, &remotelen);

    /*  If the address resolution have failed, wait and re-try. */
    if (rc < 0) {
        cstream->sink = &nn_cstream_state_waiting;
        nn_timer_start (&cstream->retry_timer,
            nn_cstream_compute_retry_ivl (cstream));
        return;
    }

    /*  Open the socket and start connecting. */
    cstream->sink = &nn_cstream_state_connecting;
    if (rc & NN_CSTREAM_DOBIND)
        nn_usock_bind (&cstream->usock, (struct sockaddr*) &local, locallen);
    nn_usock_connect (&cstream->usock, (struct sockaddr*) &remote, remotelen);
}

/******************************************************************************/
/*  State: CONNECTING                                                         */
/******************************************************************************/

static void nn_cstream_connecting_connected (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static void nn_cstream_connecting_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum);
static const struct nn_cp_sink nn_cstream_state_connecting = {
    NULL,
    NULL,
    nn_cstream_connecting_connected,
    NULL,
    nn_cstream_connecting_err,
    NULL,
    NULL,
    NULL
};

static void nn_cstream_connecting_connected (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, sink);

    /*  TODO: Set current reconnect interval to the value of
        NN_RECONNECT_IVL. */

    /*  Connect succeeded. Switch to the session state machine. */
    cstream->sink = &nn_cstream_state_connected;
    nn_stream_init (&cstream->stream, &cstream->epbase, &cstream->usock);
}

static void nn_cstream_connecting_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, sink);

    /*  Connect failed. Close the underlying socket. */
    cstream->sink = &nn_cstream_state_closing;
    nn_usock_close (&cstream->usock);
}

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the 'stream' state machine. */

static void nn_cstream_connected_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum);
static const struct nn_cp_sink nn_cstream_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    nn_cstream_connected_err,
    NULL,
    NULL,
    NULL
};

static void nn_cstream_connected_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, sink);

    /*  The connection is broken. Reconnect. */
    cstream->sink = &nn_cstream_state_waiting;
    nn_cstream_waiting_timeout (&cstream->sink, &cstream->retry_timer);
}

/******************************************************************************/
/*  State: CLOSING                                                            */
/******************************************************************************/

static void nn_cstream_closing_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static const struct nn_cp_sink nn_cstream_state_closing = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_cstream_closing_closed,
    NULL,
    NULL
};

static void nn_cstream_closing_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    int rc;
    struct nn_cstream *cstream;
    int sndbuf;
    int rcvbuf;
    size_t sz;

    cstream = nn_cont (self, struct nn_cstream, sink);

    /*  Get the current values of NN_SNDBUF and NN_RCVBUF options. */    
    sz = sizeof (sndbuf);
    nn_epbase_getopt (&cstream->epbase, NN_SOL_SOCKET, NN_SNDBUF, &sndbuf, &sz);
    nn_assert (sz == sizeof (sndbuf));
    sz = sizeof (rcvbuf);
    nn_epbase_getopt (&cstream->epbase, NN_SOL_SOCKET, NN_RCVBUF, &rcvbuf, &sz);
    nn_assert (sz == sizeof (rcvbuf));

    /*  Create new socket. */
    rc = cstream->initsockfn (&cstream->usock, sndbuf, rcvbuf,
        nn_epbase_getcp (&cstream->epbase));
    errnum_assert (rc == 0, -rc);
    nn_usock_setsink (&cstream->usock, &cstream->sink);

    /*  Wait for the specified period. */
    cstream->sink = &nn_cstream_state_waiting;
    nn_timer_start (&cstream->retry_timer,
        nn_cstream_compute_retry_ivl (cstream));
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void nn_cstream_terminating_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static const struct nn_cp_sink nn_cstream_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_cstream_terminating_closed,
    NULL,
    NULL
};

static int nn_cstream_close (struct nn_epbase *self)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, epbase);

    /*  If termination is already underway, do nothing and let it continue. */
    if (cstream->sink == &nn_cstream_state_terminating)
        return -EINPROGRESS;

    /*  If the connection exists, stop the session state machine. */
    if (cstream->sink == &nn_cstream_state_connected)
        nn_stream_term (&cstream->stream);

    /*  Deallocate resources. */
    nn_timer_term (&cstream->retry_timer);

    /*  Close the socket, if needed. */
    cstream->sink = &nn_cstream_state_terminating;
    nn_usock_close (&cstream->usock);

    return -EINPROGRESS;
}

static void nn_cstream_terminating_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_cstream *cstream;

    cstream = nn_cont (self, struct nn_cstream, sink);

    nn_epbase_term (&cstream->epbase);
    nn_free (cstream);
}

