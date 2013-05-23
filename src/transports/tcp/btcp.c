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

#include "btcp.h"
#include "atcp.h"

#include "../utils/port.h"
#include "../utils/iface.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/fast.h"

#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <netinet/in.h>

/*  The backlog is set relatively high so that there are not to many failed
    connection attemps during re-connection storms. */
#define NN_BTCP_BACKLOG 100

#define NN_BTCP_STATE_IDLE 1
#define NN_BTCP_STATE_ACTIVE 2
#define NN_BTCP_STATE_STOPPING_ATCP 3
#define NN_BTCP_STATE_STOPPING_USOCK 4
#define NN_BTCP_STATE_STOPPING_ATCPS 5

struct nn_btcp {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying listening TCP socket. */
    struct nn_usock usock;

    /*  The connection being accepted at the moment. */
    struct nn_atcp *atcp;

    /*  List of accepted connections. */
    struct nn_list atcps;
};

/*  nn_epbase virtual interface implementation. */
static void nn_btcp_stop (struct nn_epbase *self);
static void nn_btcp_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_btcp_epbase_vfptr = {
    nn_btcp_stop,
    nn_btcp_destroy
};

/*  Private functions. */
static void nn_btcp_handler (struct nn_fsm *self, void *source, int type);
static void nn_btcp_start_listening (struct nn_btcp *self);
static void nn_btcp_start_accepting (struct nn_btcp *self);

int nn_btcp_create (void *hint, struct nn_epbase **epbase)
{
    struct nn_btcp *self;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_btcp), "btcp");
    alloc_assert (self);

    /*  Initialise the structure. */
    nn_epbase_init (&self->epbase, &nn_btcp_epbase_vfptr, hint);
    nn_fsm_init_root (&self->fsm, nn_btcp_handler,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_BTCP_STATE_IDLE;
    nn_usock_init (&self->usock, &self->fsm);
    self->atcp = NULL;
    nn_list_init (&self->atcps);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_btcp_stop (struct nn_epbase *self)
{
    struct nn_btcp *btcp;

    btcp = nn_cont (self, struct nn_btcp, epbase);

    nn_fsm_stop (&btcp->fsm);
}

static void nn_btcp_destroy (struct nn_epbase *self)
{
    struct nn_btcp *btcp;

    btcp = nn_cont (self, struct nn_btcp, epbase);

    nn_assert (btcp->state == NN_BTCP_STATE_IDLE);
    nn_list_term (&btcp->atcps);
    nn_assert (btcp->atcp == NULL);
    nn_usock_term (&btcp->usock);
    nn_epbase_term (&btcp->epbase);
    nn_fsm_term (&btcp->fsm);

    nn_free (btcp);
}

static void nn_btcp_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_btcp *btcp;
    struct nn_list_item *it;
    struct nn_atcp *atcp;

    btcp = nn_cont (self, struct nn_btcp, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &btcp->fsm && type == NN_FSM_STOP)) {
        nn_atcp_stop (btcp->atcp);
        btcp->state = NN_BTCP_STATE_STOPPING_ATCP;
    }
    if (nn_slow (btcp->state == NN_BTCP_STATE_STOPPING_ATCP)) {
        if (!nn_atcp_isidle (btcp->atcp))
            return;
        nn_atcp_term (btcp->atcp);
        nn_free (btcp->atcp);
        btcp->atcp = NULL;
        nn_usock_stop (&btcp->usock);
        btcp->state = NN_BTCP_STATE_STOPPING_USOCK;
    }
    if (nn_slow (btcp->state == NN_BTCP_STATE_STOPPING_USOCK)) {
       if (!nn_usock_isidle (&btcp->usock))
            return;
        for (it = nn_list_begin (&btcp->atcps);
              it != nn_list_end (&btcp->atcps);
              it = nn_list_next (&btcp->atcps, it)) {
            atcp = nn_cont (it, struct nn_atcp, item);
            nn_atcp_stop (atcp);
        }
        btcp->state = NN_BTCP_STATE_STOPPING_ATCPS;
        goto atcps_stopping;
    }
    if (nn_slow (btcp->state == NN_BTCP_STATE_STOPPING_ATCPS)) {

        /*  The assumption here is that the events here are generated only
            by child atcp state machines. We could programatically test the
            assumption, but it would be O(n)-complex, so we'll skip the test. */
        nn_assert (type == NN_ATCP_STOPPED);
        atcp = (struct nn_atcp *) source;
        nn_list_erase (&btcp->atcps, &atcp->item);
        nn_atcp_term (atcp);
        nn_free (atcp);
        
        /*  If there are no more atcp state machines, we can stop the whole
            btcp object. */
atcps_stopping:
        if (nn_list_empty (&btcp->atcps)) {
            btcp->state = NN_BTCP_STATE_IDLE;
            nn_fsm_stopped_noevent (&btcp->fsm);
            nn_epbase_stopped (&btcp->epbase);
            return;
        }

        return;
    }

    switch (btcp->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_BTCP_STATE_IDLE:
        if (source == &btcp->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_btcp_start_listening (btcp);
                nn_btcp_start_accepting (btcp);
                btcp->state = NN_BTCP_STATE_ACTIVE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  The execution is yielded to the atcp state machine in this state.         */
/******************************************************************************/
    case NN_BTCP_STATE_ACTIVE:
        if (source == btcp->atcp) {
            switch (type) {
            case NN_ATCP_ACCEPTED:

                /*  Move the newly created connection to the list of existing
                    connections. */
                nn_list_insert (&btcp->atcps, &btcp->atcp->item,
                    nn_list_end (&btcp->atcps));
                btcp->atcp = NULL;

                /*  Start waiting for a new incoming connection. */
                nn_btcp_start_accepting (btcp);

                return;

            default:
                nn_assert (0);
            }
        }

        /*  For all remaining events we'll assume they are coming from one
            of remaining child atcp objects. */
        atcp = (struct nn_atcp*) source;
        switch (type) {
        case NN_ATCP_ERROR:
            nn_atcp_stop (atcp);
            return;
        case NN_ATCP_STOPPED:
            nn_list_erase (&btcp->atcps, &atcp->item);
            nn_atcp_term (atcp);
            nn_free (atcp);
            return;
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

static void nn_btcp_start_listening (struct nn_btcp *self)
{
    int rc;
    struct sockaddr_storage ss;
    size_t sslen;
    const char *addr;
    const char *end;
    const char *pos;
    uint16_t port;

    /*  First, resolve the IP address. */
    addr = nn_epbase_getaddr (&self->epbase);
    memset (&ss, 0, sizeof (ss));

    /*  Parse the port. */
    end = addr + strlen (addr);
    pos = strrchr (addr, ':');
    nn_assert (pos);
    ++pos;
    rc = nn_port_resolve (pos, end - pos);
    nn_assert (rc >= 0);
    port = rc;

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_iface_resolve (addr, pos - addr - 1, 1, &ss, &sslen);

    /*  TODO: In theory we could re-try in case of error, just in case the user
        configures new network interface while the application is running. */
    errnum_assert (rc == 0, -rc);

    /*  Combine the port and the address. */
    if (ss.ss_family == AF_INET) {
        ((struct sockaddr_in*) &ss)->sin_port = htons (port);
        sslen = sizeof (struct sockaddr_in);
    }
    else if (ss.ss_family == AF_INET6) {
        ((struct sockaddr_in6*) &ss)->sin6_port = htons (port);
        sslen = sizeof (struct sockaddr_in6);
    }
    else
        nn_assert (0);

    /*  Start listening for incoming connections. */
    rc = nn_usock_start (&self->usock, ss.ss_family, SOCK_STREAM, 0);
    /*  TODO: EMFILE error can happen here. We can wait a bit and re-try. */
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_bind (&self->usock, (struct sockaddr*) &ss, (size_t) sslen);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_listen (&self->usock, NN_BTCP_BACKLOG);
    errnum_assert (rc == 0, -rc);
}

static void nn_btcp_start_accepting (struct nn_btcp *self)
{
    nn_assert (self->atcp == NULL);

    /*  Allocate new atcp state machine. */
    self->atcp = nn_alloc (sizeof (struct nn_atcp), "atcp");
    alloc_assert (self->atcp);
    nn_atcp_init (self->atcp, &self->epbase, &self->fsm);

    /*  Start waiting for a new incoming connection. */
    nn_atcp_start (self->atcp, &self->usock);
}

