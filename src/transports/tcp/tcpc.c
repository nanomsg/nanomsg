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

#include "tcpc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/addr.h"

#include <string.h>

/*  Implementation of sp_epbase interface. */
static int sp_tcpc_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_tcpc_epbase_vfptr =
    {sp_tcpc_close};

/*  WAITING state. */
static void sp_tcpc_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static const struct sp_cp_sink sp_tcpc_state_waiting = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_tcpc_timeout
};

/*  CONNECTING state. */
static void sp_tcpc_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_tcpc_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);
static const struct sp_cp_sink sp_tcpc_state_connecting = {
    NULL,
    NULL,
    sp_tcpc_connected,
    NULL,
    sp_tcpc_err,
    NULL
};

int sp_tcpc_init (struct sp_tcpc *self, const char *addr, void *hint)
{
    int rc;

    /*  TODO: Check the syntax of the address and return error if it is
        not a valid address string. Don't do any blocking DNS operations
        though! */

    /*  Initialise the base class. */
    sp_epbase_init (&self->epbase, &sp_tcpc_epbase_vfptr, addr, hint);

    /*  Open a socket. */
    rc = sp_usock_init (&self->usock, &self->sink,
        AF_INET, SOCK_STREAM, IPPROTO_TCP, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);

    /*  Initialise the retry timer. */
    sp_timer_init (&self->retry_timer, &self->sink,
        sp_epbase_getcp (&self->epbase));

    /*  Pretend we were waiting for the re-connect timer and that the timer
        have expired. */
    self->sink = &sp_tcpc_state_waiting;
    sp_tcpc_timeout (&self->sink, &self->retry_timer);

    return 0;
}

static int sp_tcpc_close (struct sp_epbase *self, int linger)
{
    struct sp_tcpc *tcpc;

    tcpc = sp_cont (self, struct sp_tcpc, epbase);
    sp_assert (0);
}

static void sp_tcpc_connected (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_tcpc *tcpc;

    tcpc = sp_cont (self, struct sp_tcpc, sink);

    /*  Connect succeeded. Switch to the session state machine. */
    sp_tcps_init (&tcpc->session, &tcpc->epbase, &tcpc->usock);
}

static void sp_tcpc_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    int rc;
    struct sp_tcpc *tcpc;

    tcpc = sp_cont (self, struct sp_tcpc, sink);

    /*  Connect failed. Close the old socket and create a new one. */
    sp_usock_term (&tcpc->usock);
    rc = sp_usock_init (&tcpc->usock, &tcpc->sink,
        AF_INET, SOCK_STREAM, IPPROTO_TCP, sp_epbase_getcp (&tcpc->epbase));
    errnum_assert (rc == 0, -rc);

    /*  Switch to the WAITING state. */
    tcpc->sink = &sp_tcpc_state_waiting;
    /*  TODO: Get the retry interval from the socket option. */
    sp_timer_start (&tcpc->retry_timer, 100);
}

static void sp_tcpc_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    int rc;
    struct sp_tcpc *tcpc;
    const char *addr;
    int port;
    const char *colon;
    struct sockaddr_storage ss;
    socklen_t sslen;

    tcpc = sp_cont (self, struct sp_tcpc, sink);

    /*  Retry timer expired. Now we'll try to resolve the address. */
    addr = sp_epbase_getaddr (&tcpc->epbase);

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (&ss, 0, sizeof (ss));

    /*  Parse the port. */
    port = sp_addr_parse_port (addr, &colon);

    /*  TODO: Parse the local address, if any. */

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = sp_addr_parse_remote (addr, colon - addr, SP_ADDR_IPV4ONLY,
        &ss, &sslen);

    /*  If the address resolution have failed, wait and re-try. */
    if (rc < 0) {
        tcpc->sink = &sp_tcpc_state_waiting;
        /*  TODO: Get the retry interval from the socket option. */
        sp_timer_start (&tcpc->retry_timer, 100);
        return;
    }

    /*  Combine the port and the address. */
    if (ss.ss_family == AF_INET)
        ((struct sockaddr_in*) &ss)->sin_port = htons (port);
    else if (ss.ss_family == AF_INET6)
        ((struct sockaddr_in6*) &ss)->sin6_port = htons (port);
    else
        sp_assert (0);

    /*  TODO: New RESOLVING state should be added here to deal with
        asynchronous DNS queries. */

    /*  Open the socket and start connecting. */
    tcpc->sink = &sp_tcpc_state_connecting;
    sp_usock_connect (&tcpc->usock, (struct sockaddr*) &ss, sslen);
}

