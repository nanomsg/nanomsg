/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

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

#include <stdlib.h>
#include <limits.h>

#include "fdtcp.h"
#include "stcp.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../transport.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#define NN_FDTCP_STATE_STARTING 1
#define NN_FDTCP_STATE_ACTIVE 2
#define NN_FDTCP_STATE_STOPPING_STCP 3
#define NN_FDTCP_STATE_STOPPING_STCP_FINAL 4
#define NN_FDTCP_STATE_STOPPING 5
#define NN_FDTCP_STATE_STOPPED 6

#define NN_FDTCP_SRC_USOCK 1
#define NN_FDTCP_SRC_STCP 2

struct nn_fdtcp {
    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying TCP socket. */
    struct nn_usock usock;

    /*  State machine that handles the active part of the connection
        lifetime. */
    struct nn_stcp stcp;
};

/*  nn_epbase virtual interface implementation. */
static void nn_fdtcp_stop (struct nn_epbase *self);
static void nn_fdtcp_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_fdtcp_epbase_vfptr = {
    nn_fdtcp_stop,
    nn_fdtcp_destroy
};

static void nn_fdtcp_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_fdtcp_start_processing (struct nn_fdtcp *self);

int nn_fdtcp_create (void *hint, struct nn_epbase **epbase)
{
    int rc;
    struct nn_fdtcp *self;
    const char *addr;
    size_t addrlen;
    const char *end;
    const char *pos;
    int port;
    struct sockaddr_storage ss;
    size_t sslen;
    int ipv4only;
    size_t ipv4onlylen;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_fdtcp), "fdtcp");
    alloc_assert (self);

    /*  Initalise the epbase. */
    nn_epbase_init (&self->epbase, &nn_fdtcp_epbase_vfptr, hint);
    addr = nn_epbase_getaddr (&self->epbase);
    addrlen = strlen (addr);

    char *endptr;
    long fd = strtol (addr, &endptr, 0);
    if (nn_slow (*endptr != 0 || endptr == addr || fd > INT_MAX)) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }

    /*  Initialise the structure. */
    nn_fsm_init_root (&self->fsm, nn_fdtcp_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_FDTCP_STATE_STARTING;
    nn_usock_init (&self->usock, NN_FDTCP_SRC_USOCK, &self->fsm);
    nn_stcp_init (&self->stcp, NN_FDTCP_SRC_STCP, &self->epbase, &self->fsm);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_fdtcp_stop (struct nn_epbase *self)
{
    struct nn_fdtcp *fdtcp;

    fdtcp = nn_cont (self, struct nn_fdtcp, epbase);

    nn_fsm_stop (&fdtcp->fsm);
}

static void nn_fdtcp_destroy (struct nn_epbase *self)
{
    struct nn_fdtcp *fdtcp;

    fdtcp = nn_cont (self, struct nn_fdtcp, epbase);

    nn_stcp_term (&fdtcp->stcp);
    nn_usock_term (&fdtcp->usock);
    nn_fsm_term (&fdtcp->fsm);
    nn_epbase_term (&fdtcp->epbase);

    nn_free (fdtcp);
}

static void nn_fdtcp_start_processing (struct nn_fdtcp *self) {
    int rc;
    int fd;
    const char *addr;

    /*  First, resolve the IP address. */
    addr = nn_epbase_getaddr (&self->epbase);
    fd = atoi(addr);

    /*  Start listening for incoming connections. */
    rc = nn_usock_start_from_fd (&self->usock, fd);
    errnum_assert (rc == 0, -rc);

    nn_stcp_start (&self->stcp, &self->usock);
}

static void nn_fdtcp_handler (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_fdtcp *fdtcp;

    fdtcp = nn_cont (self, struct nn_fdtcp, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        nn_stcp_stop (&fdtcp->stcp);
        fdtcp->state = NN_FDTCP_STATE_STOPPING_STCP_FINAL;
    }
    if (nn_slow (fdtcp->state == NN_FDTCP_STATE_STOPPING_STCP_FINAL)) {
        if (!nn_stcp_isidle (&fdtcp->stcp))
            return;
        nn_usock_stop (&fdtcp->usock);
        fdtcp->state = NN_FDTCP_STATE_STOPPING;
    }
    if (nn_slow (fdtcp->state == NN_FDTCP_STATE_STOPPING)) {
        if (!nn_usock_isidle (&fdtcp->usock))
            return;
        fdtcp->state = NN_FDTCP_STATE_STOPPED;
        nn_fsm_stopped_noevent (&fdtcp->fsm);
        nn_epbase_stopped (&fdtcp->epbase);
        return;
    }

    switch (fdtcp->state) {

/******************************************************************************/
/*  STARTING state.                                                           */
/******************************************************************************/

    case NN_FDTCP_STATE_STARTING:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_fdtcp_start_processing (fdtcp);
                fdtcp->state = NN_FDTCP_STATE_ACTIVE;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }
/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  Connection is still alive and handled by the stcp state machine.          */
/******************************************************************************/
    case NN_FDTCP_STATE_ACTIVE:
        switch (src) {

        case NN_FDTCP_SRC_STCP:
            switch (type) {
            case NN_STCP_ERROR:
                nn_stcp_stop (&fdtcp->stcp);
                fdtcp->state = NN_FDTCP_STATE_STOPPING_STCP;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }
    }
}
