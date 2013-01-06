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

#include <string.h>

/*  States. */
static const struct sp_cp_sink sp_cstream_state_waiting;
static const struct sp_cp_sink sp_cstream_state_connecting;
static const struct sp_cp_sink sp_cstream_state_connected;
static const struct sp_cp_sink sp_cstream_state_closing;

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
    int (*initsockfn) (struct sp_usock *sock, struct sp_cp *cp),
    int (*resolvefn) (const char *addr, struct sockaddr_storage *ss,
    socklen_t *sslen))
{
    int rc;

    self->initsockfn = initsockfn;
    self->resolvefn = resolvefn;

    /*  TODO: Check the syntax of the address and return error if it is
        not a valid address string. Don't do any blocking DNS operations
        though! */

    /*  Initialise the base class. */
    sp_epbase_init (&self->epbase, &sp_cstream_epbase_vfptr, addr, hint);

    /*  Open a socket. */
    rc = self->initsockfn (&self->usock, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    sp_usock_setsink (&self->usock, &self->sink);

    /*  Initialise the retry timer. */
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
        /*  TODO: Get the retry interval from the socket option. */
        sp_timer_start (&cstream->retry_timer, 100);
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

    cstream = sp_cont (self, struct sp_cstream, sink);

    /*  Create new socket. */
    rc = cstream->initsockfn (&cstream->usock,
        sp_epbase_getcp (&cstream->epbase));
    errnum_assert (rc == 0, -rc);
    sp_usock_setsink (&cstream->usock, &cstream->sink);

    /*  Wait for the specified period. */
    cstream->sink = &sp_cstream_state_waiting;
    /*  TODO: Get the retry interval from the socket option. */
    sp_timer_start (&cstream->retry_timer, 100);
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

