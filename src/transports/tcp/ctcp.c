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

#include "ctcp.h"
#include "stcp.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../utils/backoff.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/addr.h"

#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define NN_CTCP_STATE_IDLE 1
#define NN_CTCP_STATE_CONNECTING 2
#define NN_CTCP_STATE_ACTIVE 3
#define NN_CTCP_STATE_STOPPING_STCP 4
#define NN_CTCP_STATE_STOPPING_USOCK 5
#define NN_CTCP_STATE_WAITING 6
#define NN_CTCP_STATE_STOPPING_BACKOFF 7
#define NN_CTCP_STATE_STOPPING_STCP_FINAL 8
#define NN_CTCP_STATE_STOPPING 9

struct nn_ctcp {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying TCP socket. */
    struct nn_usock usock;

    /*  Used to wait before retrying to connect. */
    struct nn_backoff retry;

    /*  State machine that handles the active part of the connection
        lifetime. */
    struct nn_stcp stcp;
};

/*  nn_epbase virtual interface implementation. */
static void nn_ctcp_stop (struct nn_epbase *self);
static void nn_ctcp_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_ctcp_epbase_vfptr = {
    nn_ctcp_stop,
    nn_ctcp_destroy
};

/*  Private functions. */
static void nn_ctcp_handler (struct nn_fsm *self, void *source, int type);
static void nn_ctcp_start_connecting (struct nn_ctcp *self);

int nn_ctcp_create (void *hint, struct nn_epbase **epbase)
{
    struct nn_ctcp *self;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_ctcp), "ctcp");
    alloc_assert (self);

    /*  Initialise the structure. */
    nn_epbase_init (&self->epbase, &nn_ctcp_epbase_vfptr, hint);
    nn_fsm_init_root (&self->fsm, nn_ctcp_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_CTCP_STATE_IDLE;
    nn_usock_init (&self->usock, &self->fsm);
    nn_backoff_init (&self->retry, 1000, 1000, &self->fsm);
    nn_stcp_init (&self->stcp, &self->epbase, &self->fsm);

    /*  Start the state machine. */
    nn_ctcp_start_connecting (self);
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_ctcp_stop (struct nn_epbase *self)
{
    struct nn_ctcp *ctcp;

    ctcp = nn_cont (self, struct nn_ctcp, epbase);

    nn_fsm_stop (&ctcp->fsm);
}

static void nn_ctcp_destroy (struct nn_epbase *self)
{
    struct nn_ctcp *ctcp;

    ctcp = nn_cont (self, struct nn_ctcp, epbase);

    nn_stcp_term (&ctcp->stcp);
    nn_backoff_term (&ctcp->retry);
    nn_usock_term (&ctcp->usock);
    nn_fsm_term (&ctcp->fsm);
    nn_epbase_term (&ctcp->epbase);

    nn_free (ctcp);
}

static void nn_ctcp_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_ctcp *ctcp;

    ctcp = nn_cont (self, struct nn_ctcp, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &ctcp->fsm && type == NN_FSM_STOP)) {
        nn_stcp_stop (&ctcp->stcp);
        ctcp->state = NN_CTCP_STATE_STOPPING_STCP_FINAL;
    }
    if (nn_slow (ctcp->state == NN_CTCP_STATE_STOPPING_STCP_FINAL)) {
        if (!nn_stcp_isidle (&ctcp->stcp))
            return;
        nn_backoff_stop (&ctcp->retry);
        nn_usock_stop (&ctcp->usock);
        ctcp->state = NN_CTCP_STATE_STOPPING;
    }
    if (nn_slow (ctcp->state == NN_CTCP_STATE_STOPPING)) {
        if (!nn_backoff_isidle (&ctcp->retry) ||
              !nn_usock_isidle (&ctcp->usock))
            return;
        ctcp->state = NN_CTCP_STATE_IDLE;
        nn_fsm_stopped_noevent (&ctcp->fsm);
        nn_epbase_stopped (&ctcp->epbase);
        return;
    }

    switch (ctcp->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_CTCP_STATE_IDLE:
        if (source == &ctcp->fsm) {
            switch (type) {
            case NN_FSM_START:
                ctcp->state = NN_CTCP_STATE_CONNECTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTING state.                                                         */
/*  Non-blocking connect is under way.                                        */
/******************************************************************************/
    case NN_CTCP_STATE_CONNECTING:
        if (source == &ctcp->usock) {
            switch (type) {
            case NN_USOCK_CONNECTED:
                nn_stcp_start (&ctcp->stcp, &ctcp->usock);
                ctcp->state = NN_CTCP_STATE_ACTIVE;
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&ctcp->usock);
                ctcp->state = NN_CTCP_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  Connection is established and handled by the stcp state machine.          */
/******************************************************************************/
    case NN_CTCP_STATE_ACTIVE:
        if (source == &ctcp->stcp) {
            switch (type) {
            case NN_STCP_ERROR:
                nn_stcp_stop (&ctcp->stcp);
                ctcp->state = NN_CTCP_STATE_STOPPING_STCP;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_STCP state.                                                      */
/*  stcp object was asked to stop but it haven't stopped yet.                 */
/******************************************************************************/
    case NN_CTCP_STATE_STOPPING_STCP:
        if (source == &ctcp->stcp) {
            switch (type) {
            case NN_STCP_STOPPED:
                nn_usock_stop (&ctcp->usock);
                ctcp->state = NN_CTCP_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_USOCK state.                                                     */
/*  usock object was asked to stop but it haven't stopped yet.                */
/******************************************************************************/
    case NN_CTCP_STATE_STOPPING_USOCK:
        if (source == &ctcp->usock) {
            switch (type) {
            case NN_USOCK_STOPPED:
                nn_backoff_start (&ctcp->retry);
                ctcp->state = NN_CTCP_STATE_WAITING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  WAITING state.                                                            */
/*  Waiting before re-connection is attempted. This way we won't overload     */
/*  the system by continuous re-connection attemps.                           */
/******************************************************************************/
    case NN_CTCP_STATE_WAITING:
        if (source == &ctcp->retry) {
            switch (type) {
            case NN_BACKOFF_TIMEOUT:
                nn_backoff_stop (&ctcp->retry);
                ctcp->state = NN_CTCP_STATE_STOPPING_BACKOFF;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING_BACKOFF state.                                                   */
/*  backoff object was asked to stop, but it haven't stopped yet.             */
/******************************************************************************/
    case NN_CTCP_STATE_STOPPING_BACKOFF:
        if (source == &ctcp->retry) {
            switch (type) {
            case NN_BACKOFF_STOPPED:
                nn_ctcp_start_connecting (ctcp);
                ctcp->state = NN_CTCP_STATE_CONNECTING;
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

/******************************************************************************/
/*  State machine actions.                                                    */
/******************************************************************************/

static void nn_ctcp_start_connecting (struct nn_ctcp *self)
{
    int rc;
    struct sockaddr_storage remote;
    nn_socklen remotelen;
    int uselocal;
    struct sockaddr_storage local;
    nn_socklen locallen;
    const char *addr;
    const char *end;
    const char *colon;
    const char *semicolon;
    uint16_t port;

    /*  Create IP address from the address string. */
    addr = nn_epbase_getaddr (&self->epbase);
    memset (&remote, 0, sizeof (remote));

    /*  Parse the port. */
    end = addr + strlen (addr);
    colon = strrchr (addr, ':');
    rc = nn_addr_parse_port (colon + 1, end - colon - 1);
    errnum_assert (rc > 0, -rc);
    port = rc;

    /*  Parse the local address, if any. */
    uselocal = 0;
    semicolon = strchr (addr, ';');
    if (semicolon) {
        memset (&local, 0, sizeof (local));
        rc = nn_addr_parse_local (addr, semicolon - addr, NN_ADDR_IPV4ONLY,
            &local, &locallen);
        errnum_assert (rc == 0, -rc);
        addr = semicolon + 1;
        uselocal = 1;
    }

    /*  Parse the remote address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_addr_parse_remote (addr, colon - addr, NN_ADDR_IPV4ONLY,
        &remote, &remotelen);
    errnum_assert (rc == 0, -rc);

    /*  Combine the remote address and the port. */
    if (remote.ss_family == AF_INET)
        ((struct sockaddr_in*) &remote)->sin_port = htons (port);
    else if (remote.ss_family == AF_INET6)
        ((struct sockaddr_in6*) &remote)->sin6_port = htons (port);
    else
        nn_assert (0);

    /*  Try to start the underlying socket. */
    rc = nn_usock_start (&self->usock, remote.ss_family, SOCK_STREAM, 0);
    if (nn_slow (rc < 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_CTCP_STATE_WAITING;
        return;
    }

    /*  Bind the socket to the local network interface, if specified. */
    if (uselocal) {
        rc = nn_usock_bind (&self->usock, (struct sockaddr*) &local, locallen);
        errnum_assert (rc == 0, -rc);
    }

    /*  Start connecting. */
    nn_usock_connect (&self->usock, (struct sockaddr*) &remote, remotelen);
}

