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

#if !defined SP_HAVE_WINDOWS

#include "ipcc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <string.h>
#include <sys/un.h>

/*  States. */
static const struct sp_cp_sink sp_ipcc_state_waiting;
static const struct sp_cp_sink sp_ipcc_state_connecting;
static const struct sp_cp_sink sp_ipcc_state_connected;
static const struct sp_cp_sink sp_ipcc_state_closing;

/*  Implementation of sp_epbase interface. */
static int sp_ipcc_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_ipcc_epbase_vfptr =
    {sp_ipcc_close};

/******************************************************************************/
/*  State: WAITING                                                            */
/******************************************************************************/

static void sp_ipcc_waiting_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static const struct sp_cp_sink sp_ipcc_state_waiting = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_ipcc_waiting_timeout,
    NULL
};

int sp_ipcc_init (struct sp_ipcc *self, const char *addr, void *hint)
{
    int rc;
    struct sockaddr_un *un;

    /*  Check the syntax of the address. */
    if (strlen (addr) >= sizeof (un->sun_path))
        return -ENAMETOOLONG;

    /*  Initialise the base class. */
    sp_epbase_init (&self->epbase, &sp_ipcc_epbase_vfptr, addr, hint);

    /*  Open a socket. */
    rc = sp_usock_init (&self->usock, &self->sink,
        AF_UNIX, SOCK_STREAM, 0, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);

    /*  Initialise the retry timer. */
    sp_timer_init (&self->retry_timer, &self->sink,
        sp_epbase_getcp (&self->epbase));

    /*  Pretend we were waiting for the re-connect timer and that the timer
        have expired. */
    self->sink = &sp_ipcc_state_waiting;
    sp_ipcc_waiting_timeout (&self->sink, &self->retry_timer);

    return 0;
}

static void sp_ipcc_waiting_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    int rc;
    struct sp_ipcc *ipcc;
    const char *addr;
    struct sockaddr_storage ss;
    socklen_t sslen;
    struct sockaddr_un *un;

    ipcc = sp_cont (self, struct sp_ipcc, sink);

    /*  Retry timer expired. Now we'll try to resolve the address. */
    addr = sp_epbase_getaddr (&ipcc->epbase);

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    sp_assert (strlen (addr) < sizeof (un->sun_path));
    ss.ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));
    sslen = sizeof (struct sockaddr_un);
    
    /*  Open the socket and start connecting. */
    ipcc->sink = &sp_ipcc_state_connecting;
    sp_usock_connect (&ipcc->usock, (struct sockaddr*) &ss, sslen);
}

/******************************************************************************/
/*  State: CONNECTING                                                         */
/******************************************************************************/

static void sp_ipcc_connecting_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_ipcc_connecting_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);
static const struct sp_cp_sink sp_ipcc_state_connecting = {
    NULL,
    NULL,
    sp_ipcc_connecting_connected,
    NULL,
    sp_ipcc_connecting_err,
    NULL,
    NULL,
    NULL
};

static void sp_ipcc_connecting_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_ipcc *ipcc;

    ipcc = sp_cont (self, struct sp_ipcc, sink);

    /*  Connect succeeded. Switch to the session state machine. */
    ipcc->sink = &sp_ipcc_state_connected;
    sp_tcps_init (&ipcc->session, &ipcc->epbase, &ipcc->usock);
}

static void sp_ipcc_connecting_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    struct sp_ipcc *ipcc;

    ipcc = sp_cont (self, struct sp_ipcc, sink);

    /*  Connect failed. Close the underlying socket. */
    ipcc->sink = &sp_ipcc_state_closing;
    sp_usock_close (&ipcc->usock);
}

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the tcps state machine. */

static const struct sp_cp_sink sp_ipcc_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

/******************************************************************************/
/*  State: CLOSING                                                            */
/******************************************************************************/

static void sp_ipcc_closing_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_ipcc_state_closing = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_ipcc_closing_closed,
    NULL,
    NULL
};

static void sp_ipcc_closing_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    int rc;
    struct sp_ipcc *ipcc;

    ipcc = sp_cont (self, struct sp_ipcc, sink);

    /*  Create new socket. */
    rc = sp_usock_init (&ipcc->usock, &ipcc->sink,
        AF_UNIX, SOCK_STREAM, 0, sp_epbase_getcp (&ipcc->epbase));
    errnum_assert (rc == 0, -rc);

    /*  Wait for the specified period. */
    ipcc->sink = &sp_ipcc_state_waiting;
    /*  TODO: Get the retry interval from the socket option. */
    sp_timer_start (&ipcc->retry_timer, 100);
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_ipcc_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_ipcc_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_ipcc_terminating_closed,
    NULL,
    NULL
};

static int sp_ipcc_close (struct sp_epbase *self, int linger)
{
    struct sp_ipcc *ipcc;

    ipcc = sp_cont (self, struct sp_ipcc, epbase);

    /*  If the connection exists, stop the session state machine. */
    if (ipcc->sink == &sp_ipcc_state_connected)
        sp_tcps_term (&ipcc->session);

    /*  Deallocate resources. */
    sp_timer_term (&ipcc->retry_timer);

    /*  Close the socket, if needed. */
    ipcc->sink = &sp_ipcc_state_terminating;
    sp_usock_close (&ipcc->usock);

    return 0;
}

static void sp_ipcc_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_ipcc *ipcc;

    ipcc = sp_cont (self, struct sp_ipcc, sink);

    sp_epbase_term (&ipcc->epbase);
    sp_free (ipcc);
}

#endif

