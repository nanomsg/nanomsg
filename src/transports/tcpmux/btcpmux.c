/*
    Copyright (c) 2012-2014 Martin Sustrik  All rights reserved.

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

#include "btcpmux.h"
#include "atcpmux.h"

#include "../utils/port.h"
#include "../utils/iface.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../utils/backoff.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/fast.h"
#include "../../utils/int.h"

#include <string.h>

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#include <sys/un.h>
#include <arpa/inet.h>
#endif

/*  The backlog is set relatively high so that there are not too many failed
    connection attemps during re-connection storms. */
#define NN_BTCPMUX_BACKLOG 100

#define NN_BTCPMUX_STATE_IDLE 1
#define NN_BTCPMUX_STATE_CONNECTING 2
#define NN_BTCPMUX_STATE_SENDING_BINDREQ 3
#define NN_BTCPMUX_STATE_ACTIVE 4
#define NN_BTCPMUX_STATE_STOPPING_USOCK 5
#define NN_BTCPMUX_STATE_STOPPING_ATCPMUXES 6
#define NN_BTCPMUX_STATE_LISTENING 7
#define NN_BTCPMUX_STATE_WAITING 8
#define NN_BTCPMUX_STATE_CLOSING 9
#define NN_BTCPMUX_STATE_STOPPING_BACKOFF 10

#define NN_BTCPMUX_SRC_USOCK 1
#define NN_BTCPMUX_SRC_ATCPMUX 2
#define NN_BTCPMUX_SRC_RECONNECT_TIMER 3

struct nn_btcpmux {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying listening TCPMUX socket. */
    struct nn_usock usock;

    /*  List of accepted connections. */
    struct nn_list atcpmuxes;

    /*  Used to wait before retrying to connect. */
    struct nn_backoff retry;

    /*  Service name. */
    const char *service;

    /*  Service name length, in network byte order. */
    uint16_t servicelen;

    /*  File descriptor of newly accepted connection. */
    int newfd;

    /*  Temporary buffer. */
    char code;
};

/*  nn_epbase virtual interface implementation. */
static void nn_btcpmux_stop (struct nn_epbase *self);
static void nn_btcpmux_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_btcpmux_epbase_vfptr = {
    nn_btcpmux_stop,
    nn_btcpmux_destroy
};

/*  Private functions. */
static void nn_btcpmux_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_btcpmux_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_btcpmux_start_connecting (struct nn_btcpmux *self);

int nn_btcpmux_create (void *hint, struct nn_epbase **epbase)
{
    int rc;
    struct nn_btcpmux *self;
    const char *addr;
    const char *colon;
    const char *slash;
    const char *end;
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_btcpmux), "btcpmux");
    alloc_assert (self);

    /*  Initalise the epbase. */
    nn_epbase_init (&self->epbase, &nn_btcpmux_epbase_vfptr, hint);

    /*  Parse the connection string. For now, we can only bind to all
        interfaces. */
    addr = nn_epbase_getaddr (&self->epbase);
    colon = strchr (addr, ':');
    if (nn_slow (!colon || colon - addr != 1 || addr [0] != '*')) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }
    slash = strchr (colon + 1, '/');
    if (nn_slow (!slash)) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }
    end = addr + strlen (addr);

    /*  Parse the port. */
    rc = nn_port_resolve (colon + 1, slash - (colon + 1));
    if (nn_slow (rc < 0)) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }

    /*  Store the service name. */
    self->service = slash + 1;
    self->servicelen = htons (end - (slash + 1));

    /*  Initialise the structure. */
    nn_fsm_init_root (&self->fsm, nn_btcpmux_handler, nn_btcpmux_shutdown,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_BTCPMUX_STATE_IDLE;
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
    nn_backoff_init (&self->retry, NN_BTCPMUX_SRC_RECONNECT_TIMER,
        reconnect_ivl, reconnect_ivl_max, &self->fsm);
    nn_usock_init (&self->usock, NN_BTCPMUX_SRC_USOCK, &self->fsm);
    nn_list_init (&self->atcpmuxes);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_btcpmux_stop (struct nn_epbase *self)
{
    struct nn_btcpmux *btcpmux;

    btcpmux = nn_cont (self, struct nn_btcpmux, epbase);

    nn_fsm_stop (&btcpmux->fsm);
}

static void nn_btcpmux_destroy (struct nn_epbase *self)
{
    struct nn_btcpmux *btcpmux;

    btcpmux = nn_cont (self, struct nn_btcpmux, epbase);

    nn_assert_state (btcpmux, NN_BTCPMUX_STATE_IDLE);
    nn_list_term (&btcpmux->atcpmuxes);
    nn_usock_term (&btcpmux->usock);
    nn_backoff_term (&btcpmux->retry);
    nn_epbase_term (&btcpmux->epbase);
    nn_fsm_term (&btcpmux->fsm);

    nn_free (btcpmux);
}

static void nn_btcpmux_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_btcpmux *btcpmux;
    struct nn_list_item *it;
    struct nn_atcpmux *atcpmux;

    btcpmux = nn_cont (self, struct nn_btcpmux, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        nn_backoff_stop (&btcpmux->retry);
        nn_usock_stop (&btcpmux->usock);
        btcpmux->state = NN_BTCPMUX_STATE_STOPPING_USOCK;
    }
    if (nn_slow (btcpmux->state == NN_BTCPMUX_STATE_STOPPING_USOCK)) {
       if (!nn_usock_isidle (&btcpmux->usock))
            return;
        for (it = nn_list_begin (&btcpmux->atcpmuxes);
              it != nn_list_end (&btcpmux->atcpmuxes);
              it = nn_list_next (&btcpmux->atcpmuxes, it)) {
            atcpmux = nn_cont (it, struct nn_atcpmux, item);
            nn_atcpmux_stop (atcpmux);
        }
        btcpmux->state = NN_BTCPMUX_STATE_STOPPING_ATCPMUXES;
        goto atcpmuxes_stopping;
    }
    if (nn_slow (btcpmux->state == NN_BTCPMUX_STATE_STOPPING_ATCPMUXES)) {
        nn_assert (src == NN_BTCPMUX_SRC_ATCPMUX && type == NN_ATCPMUX_STOPPED);
        atcpmux = (struct nn_atcpmux *) srcptr;
        nn_list_erase (&btcpmux->atcpmuxes, &atcpmux->item);
        nn_atcpmux_term (atcpmux);
        nn_free (atcpmux);

        /*  If there are no more atcpmux state machines, we can stop the whole
            btcpmux object. */
atcpmuxes_stopping:
        if (nn_list_empty (&btcpmux->atcpmuxes)) {
            btcpmux->state = NN_BTCPMUX_STATE_IDLE;
            nn_fsm_stopped_noevent (&btcpmux->fsm);
            nn_epbase_stopped (&btcpmux->epbase);
            return;
        }

        return;
    }

    nn_fsm_bad_action(btcpmux->state, src, type);
}

static void nn_btcpmux_handler (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_btcpmux *btcpmux;
    struct nn_atcpmux *atcpmux;
    struct nn_iovec iovecs [2];

    btcpmux = nn_cont (self, struct nn_btcpmux, fsm);

    switch (btcpmux->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_BTCPMUX_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_btcpmux_start_connecting (btcpmux);
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  CONNECTING state.                                                         */
/******************************************************************************/
    case NN_BTCPMUX_STATE_CONNECTING:
        switch (src) {
        case NN_BTCPMUX_SRC_USOCK:
            switch (type) {
            case NN_USOCK_CONNECTED:
                iovecs [0].iov_base = &btcpmux->servicelen;
                iovecs [0].iov_len = 2;
                iovecs [1].iov_base = (void*) btcpmux->service;
                iovecs [1].iov_len = ntohs (btcpmux->servicelen);
                nn_usock_send (&btcpmux->usock, iovecs, 2);
                btcpmux->state = NN_BTCPMUX_STATE_SENDING_BINDREQ;
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&btcpmux->usock);
                btcpmux->state = NN_BTCPMUX_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }
        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  SENDING_BINDREQ state.                                                    */
/******************************************************************************/
    case NN_BTCPMUX_STATE_SENDING_BINDREQ:
        switch (src) {
        case NN_BTCPMUX_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SENT:
                nn_usock_recv (&btcpmux->usock, &btcpmux->code, 1,
                    &btcpmux->newfd);
                btcpmux->state = NN_BTCPMUX_STATE_ACTIVE;
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&btcpmux->usock);
                btcpmux->state = NN_BTCPMUX_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }
        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  The execution is yielded to the atcpmux state machine in this state.      */
/******************************************************************************/
    case NN_BTCPMUX_STATE_ACTIVE:
        if (src == NN_BTCPMUX_SRC_USOCK) {
            switch (type) {
            case NN_USOCK_RECEIVED:
                if (btcpmux->code != 0 || btcpmux->newfd < 0) {
                    nn_usock_stop (&btcpmux->usock);
                    btcpmux->state = NN_BTCPMUX_STATE_STOPPING_USOCK;
                    return;
                }

                /*  Allocate new atcpmux state machine. */
                atcpmux = nn_alloc (sizeof (struct nn_atcpmux), "atcpmux");
                alloc_assert (atcpmux);
                nn_atcpmux_init (atcpmux, NN_BTCPMUX_SRC_ATCPMUX,
                   &btcpmux->epbase, &btcpmux->fsm);
                nn_atcpmux_start (atcpmux, btcpmux->newfd);

                nn_list_insert (&btcpmux->atcpmuxes, &atcpmux->item,
                    nn_list_end (&btcpmux->atcpmuxes));

                /*  Start accepting new connection straight away. */
                nn_usock_recv (&btcpmux->usock, &btcpmux->code, 1,
                    &btcpmux->newfd);
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&btcpmux->usock);
                btcpmux->state = NN_BTCPMUX_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }
        }

        /*  For all remaining events we'll assume they are coming from one
            of remaining child atcpmux objects. */
        nn_assert (src == NN_BTCPMUX_SRC_ATCPMUX);
        atcpmux = (struct nn_atcpmux*) srcptr;
        switch (type) {
        case NN_ATCPMUX_ERROR:
            nn_atcpmux_stop (atcpmux);
            return;
        case NN_ATCPMUX_STOPPED:
            nn_list_erase (&btcpmux->atcpmuxes, &atcpmux->item);
            nn_atcpmux_term (atcpmux);
            nn_free (atcpmux);
            return;
        default:
            nn_fsm_bad_action (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  CLOSING_USOCK state.                                                     */
/*  usock object was asked to stop but it haven't stopped yet.                */
/******************************************************************************/
    case NN_BTCPMUX_STATE_CLOSING:
        switch (src) {

        case NN_BTCPMUX_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_USOCK_STOPPED:
                nn_backoff_start (&btcpmux->retry);
                btcpmux->state = NN_BTCPMUX_STATE_WAITING;
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  WAITING state.                                                            */
/*  Waiting before re-bind is attempted. This way we won't overload           */
/*  the system by continuous re-bind attemps.                                 */
/******************************************************************************/
    case NN_BTCPMUX_STATE_WAITING:
        switch (src) {

        case NN_BTCPMUX_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_TIMEOUT:
                nn_backoff_stop (&btcpmux->retry);
                btcpmux->state = NN_BTCPMUX_STATE_STOPPING_BACKOFF;
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_BACKOFF state.                                                   */
/*  backoff object was asked to stop, but it haven't stopped yet.             */
/******************************************************************************/
    case NN_BTCPMUX_STATE_STOPPING_BACKOFF:
        switch (src) {

        case NN_BTCPMUX_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_STOPPED:
                nn_btcpmux_start_connecting (btcpmux);
                return;
            default:
                nn_fsm_bad_action (btcpmux->state, src, type);
            }

        default:
            nn_fsm_bad_source (btcpmux->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (btcpmux->state, src, type);
    }
}

/******************************************************************************/
/*  State machine actions.                                                    */
/******************************************************************************/

static void nn_btcpmux_start_connecting (struct nn_btcpmux *self)
{
    int rc;
    struct sockaddr_storage ss;
    struct sockaddr_un *un;
    const char *addr;
    const char *colon;
    const char *slash;
    int port;

    /*  Try to start the underlying socket. */
    rc = nn_usock_start (&self->usock, AF_UNIX, SOCK_STREAM, 0);
    if (nn_slow (rc < 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_BTCPMUX_STATE_WAITING;
        return;
    }

    /*  Create the IPC address from the address string. */
    addr = nn_epbase_getaddr (&self->epbase);
    colon = strchr (addr, ':');
    slash = strchr (colon + 1, '/');

    port = nn_port_resolve (colon + 1, slash - (colon + 1));
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    ss.ss_family = AF_UNIX;
    sprintf (un->sun_path, "/tmp/tcpmux-%d.ipc", (int) port);

    /*  Start connecting. */
    nn_usock_connect (&self->usock, (struct sockaddr*) &ss,
        sizeof (struct sockaddr_un));
    self->state  = NN_BTCPMUX_STATE_CONNECTING;
}

