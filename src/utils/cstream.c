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
static const struct sp_cp_sink sp_cstream_state_waiting;
static const struct sp_cp_sink sp_cstream_state_connecting;
static const struct sp_cp_sink sp_cstream_state_connected;
static const struct sp_cp_sink sp_cstream_state_closing;

/*  Private functions. */
static int sp_cstream_compute_retry_ivl (struct sp_cstream *self)
{
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;
    int result;
    unsigned int random;

    /*  Get relevant options' values. */
    sz = sizeof (reconnect_ivl);
    sp_epbase_getopt (&self->epbase, SP_SOL_SOCKET, SP_RECONNECT_IVL,
        &reconnect_ivl, &sz);
    sp_assert (sz == sizeof (reconnect_ivl));
    sz = sizeof (reconnect_ivl_max);
    sp_epbase_getopt (&self->epbase, SP_SOL_SOCKET, SP_RECONNECT_IVL_MAX,
        &reconnect_ivl_max, &sz);
    sp_assert (sz == sizeof (reconnect_ivl_max));

    /*  Negative number means that reconnect sequence is starting.
        The reconnect interval in this case is SP_RECONNECT_IVL. */
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
    sp_random_generate (&random, sizeof (random));
    result += (random % result % 1000);
    return result;
}

/*  Implementation of sp_epbase interface. */
static void sp_cstream_close (struct sp_epbase *self);
static const struct sp_epbase_vfptr sp_cstream_epbase_vfptr =
    {sp_cstream_close};

/******************************************************************************/
/*  State: WAITING                                                            */
/******************************************************************************/

static void sp_cstream_waiting_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static const struct sp_cp_sink sp_cstream_state_waiting = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_cstream_waiting_timeout,
    NULL
};

int sp_cstream_init (struct sp_cstream *self, const char *addr, void *hint,
    int (*initsockfn) (struct sp_usock *sock, int sndbuf, int rcvbuf,
    struct sp_cp *cp), int (*resolvefn) (const char *addr,
    struct sockaddr_storage *ss, socklen_t *sslen))
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
    sp_epbase_init (&self->epbase, &sp_cstream_epbase_vfptr, addr, hint);

    /*  Get the current values of SP_SNDBUF and SP_RCVBUF options. */    
    sz = sizeof (sndbuf);
    sp_epbase_getopt (&self->epbase, SP_SOL_SOCKET, SP_SNDBUF, &sndbuf, &sz);
    sp_assert (sz == sizeof (sndbuf));
    sz = sizeof (rcvbuf);
    sp_epbase_getopt (&self->epbase, SP_SOL_SOCKET, SP_RCVBUF, &rcvbuf, &sz);
    sp_assert (sz == sizeof (rcvbuf));

    /*  Open a socket. */
    rc = self->initsockfn (&self->usock, sndbuf, rcvbuf,
        sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    sp_usock_setsink (&self->usock, &self->sink);

    /*  Initialise the retry timer. */
    self->retry_ivl = -1;
    sp_timer_init (&self->retry_timer, &self->sink,
        sp_epbase_getcp (&self->epbase));

    /*  Pretend we were waiting for the re-connect timer and that the timer
        have expired. */
    self->sink = &sp_cstream_state_waiting;
    sp_cstream_waiting_timeout (&self->sink, &self->retry_timer);

    return 0;
}

static void sp_cstream_waiting_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    int rc;
    struct sp_cstream *cstream;
    struct sockaddr_storage ss;
    socklen_t sslen;

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  Retry timer expired. Now we'll try to resolve the address. */
    rc = cstream->resolvefn (sp_epbase_getaddr (&cstream->epbase), &ss, &sslen);

    /*  If the address resolution have failed, wait and re-try. */
    if (rc < 0) {
        cstream->sink = &sp_cstream_state_waiting;
        sp_timer_start (&cstream->retry_timer,
            sp_cstream_compute_retry_ivl (cstream));
        return;
    }

    /*  Open the socket and start connecting. */
    cstream->sink = &sp_cstream_state_connecting;
    sp_usock_connect (&cstream->usock, (struct sockaddr*) &ss, sslen);
}

/******************************************************************************/
/*  State: CONNECTING                                                         */
/******************************************************************************/

static void sp_cstream_connecting_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_cstream_connecting_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);
static const struct sp_cp_sink sp_cstream_state_connecting = {
    NULL,
    NULL,
    sp_cstream_connecting_connected,
    NULL,
    sp_cstream_connecting_err,
    NULL,
    NULL,
    NULL
};

static void sp_cstream_connecting_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_cstream *cstream;

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  Set current reconnect interval to the value of SP_RECONNECT_IVL. */

    /*  Connect succeeded. Switch to the session state machine. */
    cstream->sink = &sp_cstream_state_connected;
    sp_stream_init (&cstream->stream, &cstream->epbase, &cstream->usock);
}

static void sp_cstream_connecting_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    struct sp_cstream *cstream;

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  Connect failed. Close the underlying socket. */
    cstream->sink = &sp_cstream_state_closing;
    sp_usock_close (&cstream->usock);
}

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the 'stream' state machine. */

static void sp_cstream_connected_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);
static const struct sp_cp_sink sp_cstream_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    sp_cstream_connected_err,
    NULL,
    NULL,
    NULL
};

static void sp_cstream_connected_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    struct sp_cstream *cstream;

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  The connection is broken. Reconnect. */
    cstream->sink = &sp_cstream_state_waiting;
    sp_cstream_waiting_timeout (&cstream->sink, &cstream->retry_timer);
}

/******************************************************************************/
/*  State: CLOSING                                                            */
/******************************************************************************/

static void sp_cstream_closing_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_cstream_state_closing = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_cstream_closing_closed,
    NULL,
    NULL
};

static void sp_cstream_closing_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    int rc;
    struct sp_cstream *cstream;
    int sndbuf;
    int rcvbuf;
    size_t sz;

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  Get the current values of SP_SNDBUF and SP_RCVBUF options. */    
    sz = sizeof (sndbuf);
    sp_epbase_getopt (&cstream->epbase, SP_SOL_SOCKET, SP_SNDBUF, &sndbuf, &sz);
    sp_assert (sz == sizeof (sndbuf));
    sz = sizeof (rcvbuf);
    sp_epbase_getopt (&cstream->epbase, SP_SOL_SOCKET, SP_RCVBUF, &rcvbuf, &sz);
    sp_assert (sz == sizeof (rcvbuf));

    /*  Create new socket. */
    rc = cstream->initsockfn (&cstream->usock, sndbuf, rcvbuf,
        sp_epbase_getcp (&cstream->epbase));
    errnum_assert (rc == 0, -rc);
    sp_usock_setsink (&cstream->usock, &cstream->sink);

    /*  Wait for the specified period. */
    cstream->sink = &sp_cstream_state_waiting;
    sp_timer_start (&cstream->retry_timer,
        sp_cstream_compute_retry_ivl (cstream));
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_cstream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_cstream_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_cstream_terminating_closed,
    NULL,
    NULL
};

static void sp_cstream_close (struct sp_epbase *self)
{
    struct sp_cstream *cstream;
    int linger;
    size_t lingersz;

    cstream = sp_cont (self, struct sp_cstream, epbase);

    /*  If termination is already underway, do nothing and let it continue. */
    if (cstream->sink == &sp_cstream_state_terminating)
        return;

    /*  Get the current value of the linger option. */
    lingersz = sizeof (linger);
    sp_epbase_getopt (&cstream->epbase, SP_SOL_SOCKET, SP_LINGER,
        &linger, &lingersz);
    sp_assert (lingersz == sizeof (linger));
    /*  TODO: Use the linger value to set the linger timer. */

    /*  If the connection exists, stop the session state machine. */
    if (cstream->sink == &sp_cstream_state_connected)
        sp_stream_term (&cstream->stream);

    /*  Deallocate resources. */
    sp_timer_term (&cstream->retry_timer);

    /*  Close the socket, if needed. */
    cstream->sink = &sp_cstream_state_terminating;
    sp_usock_close (&cstream->usock);
}

static void sp_cstream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_cstream *cstream;

    cstream = sp_cont (self, struct sp_cstream, sink);

    sp_epbase_term (&cstream->epbase);
    sp_free (cstream);
}

