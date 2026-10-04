/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.
    Copyright 2016 Franklin "Snaipe" Mathieu <franklinmathieu@gmail.com>
    Copyright 2016 Garrett D'Amore <garrett@damore.org>
    Copyright 2026 Staysail Systems, Inc.

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

#include "bipc.h"
#include "ipc.h"
#include "aipc.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/fast.h"

#include <string.h>
#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#include <sys/un.h>
#include <fcntl.h>
#endif

#define NN_BIPC_BACKLOG 10

#define NN_BIPC_STATE_IDLE 1
#define NN_BIPC_STATE_ACTIVE 2
#define NN_BIPC_STATE_STOPPING_AIPC 3
#define NN_BIPC_STATE_STOPPING_USOCK 4
#define NN_BIPC_STATE_STOPPING_AIPCS 5

#define NN_BIPC_SRC_USOCK 1
#define NN_BIPC_SRC_AIPC 2
#define NN_BIPC_SRC_LISTEN_ERROR 3

struct nn_bipc {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;
    int failed;
    struct nn_ipc_file file;
    struct nn_fsm_event listen_error;

    struct nn_ep *ep;
    int domain;

    /*  The underlying listening IPC socket. */
    struct nn_usock usock;

    /*  The connection being accepted at the moment. */
    struct nn_aipc *aipc;

    /*  List of accepted connections. */
    struct nn_list aipcs;
};

/*  nn_ep virtual interface implementation. */
static void nn_bipc_stop (void *self);
static void nn_bipc_destroy (void *self);
const struct nn_ep_ops nn_bipc_ep_ops = {
    nn_bipc_stop,
    nn_bipc_destroy
};

/*  Private functions. */
static void nn_bipc_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_bipc_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);
static int nn_bipc_listen (struct nn_bipc *self);
static void nn_bipc_start_accepting (struct nn_bipc *self);

int nn_bipc_create (struct nn_ep *ep, int domain)
{
    struct nn_bipc *self;
    int rc;
    struct sockaddr_storage ss;

    rc = nn_ipc_resolve (nn_ep_getaddr (ep), domain, &ss);
    if (rc < 0)
        return rc;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_bipc), "bipc");
    alloc_assert (self);


    /*  Initialise the structure. */
    self->ep = ep;
    self->domain = domain;
    nn_ep_tran_setup (ep, &nn_bipc_ep_ops, self);
    nn_fsm_init_root (&self->fsm, nn_bipc_handler, nn_bipc_shutdown,
        nn_ep_getctx (ep));
    self->state = NN_BIPC_STATE_IDLE;
    self->failed = 0;
    nn_ipc_file_init (&self->file);
    nn_fsm_event_init (&self->listen_error);
    self->aipc = NULL;
    nn_list_init (&self->aipcs);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    nn_usock_init (&self->usock, NN_BIPC_SRC_USOCK, &self->fsm);

    rc = nn_bipc_listen (self);
    if (rc != 0) {
        /*  Drain the socket's stop event before freeing the endpoint. */
        self->failed = 1;
        nn_fsm_raise_from_src (&self->fsm, &self->listen_error,
            NN_BIPC_SRC_LISTEN_ERROR, 1);
        return rc;
    }

    return 0;
}

static void nn_bipc_stop (void *self)
{
    struct nn_bipc *bipc = self;

    nn_fsm_stop (&bipc->fsm);
}

static void nn_bipc_destroy (void *self)
{
    struct nn_bipc *bipc = self;

    nn_assert_state (bipc, NN_BIPC_STATE_IDLE);
    nn_list_term (&bipc->aipcs);
    nn_assert (bipc->aipc == NULL);
    nn_usock_term (&bipc->usock);
    nn_ipc_file_term (&bipc->file);
    nn_fsm_event_term (&bipc->listen_error);
    nn_fsm_term (&bipc->fsm);

    nn_free (bipc);
}

static void nn_bipc_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_bipc *bipc;
    struct nn_list_item *it;
    struct nn_aipc *aipc;

    bipc = nn_cont (self, struct nn_bipc, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        if (bipc->failed) {
            bipc->state = NN_BIPC_STATE_IDLE;
            nn_fsm_stopped_noevent (&bipc->fsm);
            return;
        }
#if !defined NN_HAVE_WINDOWS
        /*  Unlink while the listener is alive: another binder must not
            recover the name between listener close and our cleanup. */
        if (bipc->domain == AF_UNIX)
            nn_ipc_file_unlink (&bipc->file, nn_ep_getaddr (bipc->ep));
#endif
        if (bipc->aipc) {
            nn_aipc_stop (bipc->aipc);
            bipc->state = NN_BIPC_STATE_STOPPING_AIPC;
        }
        else {
            bipc->state = NN_BIPC_STATE_STOPPING_USOCK;
        }
    }
    if (nn_slow (bipc->state == NN_BIPC_STATE_STOPPING_AIPC)) {
        if (!nn_aipc_isidle (bipc->aipc))
            return;
        nn_aipc_term (bipc->aipc);
        nn_free (bipc->aipc);
        bipc->aipc = NULL;

        nn_usock_stop (&bipc->usock);
        bipc->state = NN_BIPC_STATE_STOPPING_USOCK;
    }
    if (nn_slow (bipc->state == NN_BIPC_STATE_STOPPING_USOCK)) {
       if (!nn_usock_isidle (&bipc->usock))
            return;
        for (it = nn_list_begin (&bipc->aipcs);
              it != nn_list_end (&bipc->aipcs);
              it = nn_list_next (&bipc->aipcs, it)) {
            aipc = nn_cont (it, struct nn_aipc, item);
            nn_aipc_stop (aipc);
        }
        bipc->state = NN_BIPC_STATE_STOPPING_AIPCS;
        goto aipcs_stopping;
    }
    if (nn_slow (bipc->state == NN_BIPC_STATE_STOPPING_AIPCS)) {
        nn_assert (src == NN_BIPC_SRC_AIPC && type == NN_AIPC_STOPPED);
        aipc = (struct nn_aipc *) srcptr;
        nn_list_erase (&bipc->aipcs, &aipc->item);
        nn_aipc_term (aipc);
        nn_free (aipc);

        /*  If there are no more aipc state machines, we can stop the whole
            bipc object. */
aipcs_stopping:
        if (nn_list_empty (&bipc->aipcs)) {
#if defined NN_HAVE_WINDOWS
            /*  Windows cleanup uses the captured file handle after all
                sockets close, never a fresh lookup of the old pathname. */
            if (bipc->domain == AF_UNIX)
                nn_ipc_file_unlink (&bipc->file, nn_ep_getaddr (bipc->ep));
#endif
            bipc->state = NN_BIPC_STATE_IDLE;
            nn_fsm_stopped_noevent (&bipc->fsm);
            nn_ep_stopped (bipc->ep);
            return;
        }

        return;
    }

    nn_fsm_bad_state(bipc->state, src, type);
}

static void nn_bipc_handler (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    struct nn_bipc *bipc;
    struct nn_aipc *aipc;

    bipc = nn_cont (self, struct nn_bipc, fsm);

    switch (bipc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_BIPC_STATE_IDLE:
        nn_assert (src == NN_FSM_ACTION);
        nn_assert (type == NN_FSM_START);
        bipc->state = NN_BIPC_STATE_ACTIVE;
        return;

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  The execution is yielded to the aipc state machine in this state.         */
/******************************************************************************/
    case NN_BIPC_STATE_ACTIVE:
        if (src == NN_BIPC_SRC_LISTEN_ERROR) {
            nn_fsm_stop (&bipc->fsm);
            nn_bipc_destroy (bipc);
            return;
        }
        if (src == NN_BIPC_SRC_USOCK) {
            nn_assert (type == NN_USOCK_SHUTDOWN || type == NN_USOCK_STOPPED);
            return;
        }

        /* All other events come from child aipc objects. */
        nn_assert (src == NN_BIPC_SRC_AIPC);
        aipc = (struct nn_aipc*) srcptr;
        switch (type) {
        case NN_AIPC_ACCEPTED:

            nn_list_insert (&bipc->aipcs, &aipc->item,
                nn_list_end (&bipc->aipcs));
            bipc->aipc = NULL;
            nn_bipc_start_accepting (bipc);
            return;
        case NN_AIPC_ERROR:
            nn_aipc_stop (aipc);
            return;
        case NN_AIPC_STOPPED:
            nn_list_erase (&bipc->aipcs, &aipc->item);
            nn_aipc_term (aipc);
            nn_free (aipc);
            return;
        default:
            nn_fsm_bad_action (bipc->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (bipc->state, src, type);
    }
}

static int nn_bipc_listen (struct nn_bipc *self)
{
    int rc;
    struct sockaddr_storage ss;
    int addrlen;
    const char *addr;
#if defined NN_HAVE_UNIX_SOCKETS
    int fd;
#endif

    addr = nn_ep_getaddr (self->ep);
    addrlen = nn_ipc_resolve (addr, self->domain, &ss);
    nn_assert (addrlen > 0);

    /*  Remove a stale socket file only when no listener is using it. */
#if defined NN_HAVE_WINDOWS
    if (self->domain == AF_UNIX) {
        SOCKET probe;
        u_long nonblocking = 1;
        int error;
        int errorlen;
        fd_set writable;
        fd_set failed;
        struct timeval timeout = {0, 100000};

        probe = socket (AF_UNIX, SOCK_STREAM, 0);
        if (probe != INVALID_SOCKET) {
            rc = ioctlsocket (probe, FIONBIO, &nonblocking);
            wsa_assert (rc == 0);
            rc = connect (probe, (struct sockaddr *) &ss, addrlen);
            error = rc == SOCKET_ERROR ? WSAGetLastError () : 0;
            if (error == WSAEWOULDBLOCK) {
                /*  A nonblocking connect can report its error later. */
                FD_ZERO (&writable);
                FD_ZERO (&failed);
                FD_SET (probe, &writable);
                FD_SET (probe, &failed);
                rc = select (0, NULL, &writable, &failed, &timeout);
                if (rc > 0) {
                    errorlen = sizeof (error);
                    rc = getsockopt (probe, SOL_SOCKET, SO_ERROR,
                        (char *) &error, &errorlen);
                    if (rc == SOCKET_ERROR)
                        error = WSAGetLastError ();
                }
            }
            if (error == WSAECONNREFUSED)
                nn_ipc_unlink (addr);
            closesocket (probe);
        }
    }
#elif defined NN_HAVE_UNIX_SOCKETS
    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd >= 0) {
        rc = fcntl (fd, F_SETFL, O_NONBLOCK);
        errno_assert (rc != -1 || errno == EINVAL);
        rc = connect (fd, (struct sockaddr*) &ss, addrlen);
        if (rc == -1 && errno == ECONNREFUSED)
            nn_ipc_unlink (addr);
        rc = close (fd);
        errno_assert (rc == 0);
    }
#endif

    /*  Start listening for incoming connections. */
    rc = nn_usock_start (&self->usock, self->domain, SOCK_STREAM, 0);
    if (rc < 0) {
        return rc;
    }

    rc = nn_usock_bind (&self->usock,
        (struct sockaddr*) &ss, addrlen);
    if (rc < 0) {
        nn_usock_stop (&self->usock);
        return rc;
    }

    if (self->domain == AF_UNIX)
        nn_ipc_file_capture (&self->file, addr);
    rc = nn_usock_listen (&self->usock, NN_BIPC_BACKLOG);
    if (rc < 0) {
#if !defined NN_HAVE_WINDOWS
        if (self->domain == AF_UNIX)
            nn_ipc_file_unlink (&self->file, addr);
#endif
        nn_usock_stop (&self->usock);
#if defined NN_HAVE_WINDOWS
        if (self->domain == AF_UNIX)
            nn_ipc_file_unlink (&self->file, addr);
#endif
        return rc;
    }
    nn_bipc_start_accepting (self);

    return 0;
}

/******************************************************************************/
/*  State machine actions.                                                    */
/******************************************************************************/

static void nn_bipc_start_accepting (struct nn_bipc *self)
{
    nn_assert (self->aipc == NULL);

    /*  Allocate new aipc state machine. */
    self->aipc = nn_alloc (sizeof (struct nn_aipc), "aipc");
    alloc_assert (self->aipc);
    nn_aipc_init (self->aipc, NN_BIPC_SRC_AIPC, self->ep, &self->fsm);

    /*  Start waiting for a new incoming connection. */
    nn_aipc_start (self->aipc, &self->usock);
}
