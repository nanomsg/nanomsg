/*
    Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.

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

#include "cipc.h"
#include "sipc.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../utils/backoff.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"

#include <string.h>
#include <unistd.h>
#include <sys/un.h>

#define NN_CIPC_STATE_IDLE 1
#define NN_CIPC_STATE_CONNECTING 2
#define NN_CIPC_STATE_ACTIVE 3
#define NN_CIPC_STATE_STOPPING_SIPC 4
#define NN_CIPC_STATE_STOPPING_USOCK 5
#define NN_CIPC_STATE_WAITING 6
#define NN_CIPC_STATE_STOPPING_BACKOFF 7
#define NN_CIPC_STATE_STOPPING_SIPC_FINAL 8
#define NN_CIPC_STATE_STOPPING 9

#define NN_CIPC_SRC_USOCK 1
#define NN_CIPC_SRC_RECONNECT_TIMER 2
#define NN_CIPC_SRC_SIPC 3

struct nn_cipc {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying IPC socket. */
    struct nn_usock usock;

    /*  Used to wait before retrying to connect. */
    struct nn_backoff retry;

    /*  State machine that handles the active part of the connection
        lifetime. */
    struct nn_sipc sipc;
};

/*  nn_epbase virtual interface implementation. */
static void nn_cipc_stop (struct nn_epbase *self);
static void nn_cipc_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_cipc_epbase_vfptr = {
    nn_cipc_stop,
    nn_cipc_destroy
};

/*  Private functions. */
static void nn_cipc_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_cipc_start_connecting (struct nn_cipc *self);

int nn_cipc_create (void *hint, struct nn_epbase **epbase)
{
    struct nn_cipc *self;
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_cipc), "cipc");
    alloc_assert (self);

    /*  Initialise the structure. */
    nn_epbase_init (&self->epbase, &nn_cipc_epbase_vfptr, hint);
    nn_fsm_init_root (&self->fsm, nn_cipc_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_CIPC_STATE_IDLE;
    nn_usock_init (&self->usock, NN_CIPC_SRC_USOCK, &self->fsm);
    sz = sizeof (reconnect_ivl);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL,
        &reconnect_ivl, &sz);
    nn_assert (sz == sizeof (reconnect_ivl));
    sz = sizeof (reconnect_ivl_max);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL_MAX,
        &reconnect_ivl_max, &sz);
    nn_assert (sz == sizeof (reconnect_ivl_max));
    if (reconnect_ivl_max == 0)
        reconnect_ivl_max = reconnect_ivl;
    nn_backoff_init (&self->retry, NN_CIPC_SRC_RECONNECT_TIMER,
        reconnect_ivl, reconnect_ivl_max, &self->fsm);
    nn_sipc_init (&self->sipc, NN_CIPC_SRC_SIPC, &self->epbase, &self->fsm);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_cipc_stop (struct nn_epbase *self)
{
    struct nn_cipc *cipc;

    cipc = nn_cont (self, struct nn_cipc, epbase);

    nn_fsm_stop (&cipc->fsm);
}

static void nn_cipc_destroy (struct nn_epbase *self)
{
    struct nn_cipc *cipc;

    cipc = nn_cont (self, struct nn_cipc, epbase);

    nn_sipc_term (&cipc->sipc);
    nn_backoff_term (&cipc->retry);
    nn_usock_term (&cipc->usock);
    nn_fsm_term (&cipc->fsm);
    nn_epbase_term (&cipc->epbase);

    nn_free (cipc);
}

static void nn_cipc_handler (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_cipc *cipc;

    cipc = nn_cont (self, struct nn_cipc, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        nn_sipc_stop (&cipc->sipc);
        cipc->state = NN_CIPC_STATE_STOPPING_SIPC_FINAL;
    }
    if (nn_slow (cipc->state == NN_CIPC_STATE_STOPPING_SIPC_FINAL)) {
        if (!nn_sipc_isidle (&cipc->sipc))
            return;
        nn_backoff_stop (&cipc->retry);
        nn_usock_stop (&cipc->usock);
        cipc->state = NN_CIPC_STATE_STOPPING;
    }
    if (nn_slow (cipc->state == NN_CIPC_STATE_STOPPING)) {
        if (!nn_backoff_isidle (&cipc->retry) ||
              !nn_usock_isidle (&cipc->usock))
            return;
        cipc->state = NN_CIPC_STATE_IDLE;
        nn_fsm_stopped_noevent (&cipc->fsm);
        nn_epbase_stopped (&cipc->epbase);
        return;
    }

    switch (cipc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_CIPC_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_cipc_start_connecting (cipc);
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  CONNECTING state.                                                         */
/*  Non-blocking connect is under way.                                        */
/******************************************************************************/
    case NN_CIPC_STATE_CONNECTING:
        switch (src) {

        case NN_CIPC_SRC_USOCK:
            switch (type) {
            case NN_USOCK_CONNECTED:
                nn_sipc_start (&cipc->sipc, &cipc->usock);
                cipc->state = NN_CIPC_STATE_ACTIVE;
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&cipc->usock);
                cipc->state = NN_CIPC_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  Connection is established and handled by the sipc state machine.          */
/******************************************************************************/
    case NN_CIPC_STATE_ACTIVE:
        switch (src) {

        case NN_CIPC_SRC_SIPC:
            switch (type) {
            case NN_SIPC_ERROR:
                nn_sipc_stop (&cipc->sipc);
                cipc->state = NN_CIPC_STATE_STOPPING_SIPC;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  STOPPING_SIPC state.                                                      */
/*  sipc object was asked to stop but it haven't stopped yet.                 */
/******************************************************************************/
    case NN_CIPC_STATE_STOPPING_SIPC:
        switch (src) {

        case NN_CIPC_SRC_SIPC:
            switch (type) {
            case NN_SIPC_STOPPED:
                nn_usock_stop (&cipc->usock);
                cipc->state = NN_CIPC_STATE_STOPPING_USOCK;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  STOPPING_USOCK state.                                                     */
/*  usock object was asked to stop but it haven't stopped yet.                */
/******************************************************************************/
    case NN_CIPC_STATE_STOPPING_USOCK:
        switch (src) {

        case NN_CIPC_SRC_USOCK:
            switch (type) {
            case NN_USOCK_STOPPED:
                nn_backoff_start (&cipc->retry);
                cipc->state = NN_CIPC_STATE_WAITING;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  WAITING state.                                                            */
/*  Waiting before re-connection is attempted. This way we won't overload     */
/*  the system by continuous re-connection attemps.                           */
/******************************************************************************/
    case NN_CIPC_STATE_WAITING:
        switch (src) {

        case NN_CIPC_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_TIMEOUT:
                nn_backoff_stop (&cipc->retry);
                cipc->state = NN_CIPC_STATE_STOPPING_BACKOFF;
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  STOPPING_BACKOFF state.                                                   */
/*  backoff object was asked to stop, but it haven't stopped yet.             */
/******************************************************************************/
    case NN_CIPC_STATE_STOPPING_BACKOFF:
        switch (src) {

        case NN_CIPC_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_STOPPED:
                nn_cipc_start_connecting (cipc);
                return;
            default:
                nn_assert (0);
            }

        default:
            nn_assert (0);
        }

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

static void nn_cipc_start_connecting (struct nn_cipc *self)
{
    int rc;
    struct sockaddr_storage ss;
    struct sockaddr_un *un;
    const char *addr;

    /*  Try to start the underlying socket. */
    rc = nn_usock_start (&self->usock, AF_UNIX, SOCK_STREAM, 0);
    if (nn_slow (rc < 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_CIPC_STATE_WAITING;
        return;
    }

    /*  Create the IPC address from the address string. */
    addr = nn_epbase_getaddr (&self->epbase);
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    nn_assert (strlen (addr) < sizeof (un->sun_path));
    ss.ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));

    /*  Start connecting. */
    nn_usock_connect (&self->usock, (struct sockaddr*) &ss,
        sizeof (struct sockaddr_un));
    self->state  = NN_CIPC_STATE_CONNECTING;
}

#endif

