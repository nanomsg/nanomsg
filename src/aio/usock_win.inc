/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "worker.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/alloc.h"

#include <stddef.h>
#include <string.h>

#define NN_USOCK_STATE_IDLE 1
#define NN_USOCK_STATE_CONNECTING 2
#define NN_USOCK_STATE_STARTING 3
#define NN_USOCK_STATE_LISTENING 4
#define NN_USOCK_STATE_ACCEPTING 5
#define NN_USOCK_STATE_BEING_ACCEPTED 6
#define NN_USOCK_STATE_ACCEPTED 7
#define NN_USOCK_STATE_DONE 8
#define NN_USOCK_STATE_STOPPING 9

#define NN_USOCK_EVENT_CONNECT 1
#define NN_USOCK_EVENT_LISTEN 2
#define NN_USOCK_EVENT_ACCEPT 3
#define NN_USOCK_EVENT_BEING_ACCEPTED 4
#define NN_USOCK_EVENT_ACTIVATE 5
#define NN_USOCK_EVENT_DONE 5
#define NN_USOCK_EVENT_ERROR 6

/*  Private functions. */
void nn_usock_handler (struct nn_fsm *self, void *source, int type);

void nn_usock_init (struct nn_usock *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_usock_handler, owner);
    self->state = NN_USOCK_STATE_IDLE;
    self->s = INVALID_SOCKET;
    nn_worker_op_init (&self->in, &self->fsm);
    nn_worker_op_init (&self->out, &self->fsm);
    self->domain = -1;
    self->type = -1;
    self->protocol = -1;
    self->newsock = NULL;
    self->info = NULL;

    /*  Intialise events raised by usock. */
    nn_fsm_event_init (&self->event_established);
    nn_fsm_event_init (&self->event_sent);
    nn_fsm_event_init (&self->event_received);
    nn_fsm_event_init (&self->event_error);
}

void nn_usock_term (struct nn_usock *self)
{
    nn_assert (self->state == NN_USOCK_STATE_IDLE);

    if (self->info)
        nn_free (self->info);
    nn_fsm_event_term (&self->event_error);
    nn_fsm_event_term (&self->event_received);
    nn_fsm_event_term (&self->event_sent);
    nn_fsm_event_term (&self->event_established);
    nn_worker_op_term (&self->out);
    nn_worker_op_term (&self->in);
    nn_fsm_term (&self->fsm);
}

int nn_usock_isidle (struct nn_usock *self)
{
    return nn_fsm_isidle (&self->fsm);
}

int nn_usock_start (struct nn_usock *self, int domain, int type, int protocol)
{
    int rc;
#if defined IPV6_V6ONLY
    DWORD only;
#endif
#if defined HANDLE_FLAG_INHERIT
    BOOL brc;
#endif
    struct nn_worker *worker;
    HANDLE cp;

    /*  Open the underlying socket. */
    self->s = socket (domain, type, protocol);
    if (self->s == INVALID_SOCKET)
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    /*  Disable inheriting the socket to the child processes. */
#if defined HANDLE_FLAG_INHERIT
    brc = SetHandleInformation ((HANDLE) self->s, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);
#endif

    /*  IPv4 mapping for IPv6 sockets is disabled by default. Switch it on. */
#if defined IPV6_V6ONLY
    if (domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->s, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char*) &only, sizeof (only));
        wsa_assert (rc != SOCKET_ERROR);
    }
#endif

    /*  Associate the socket with a worker thread/completion port. */
    worker = nn_fsm_choose_worker (&self->fsm);
    cp = CreateIoCompletionPort ((HANDLE) self->s,
        nn_worker_getcp (worker), (ULONG_PTR) NULL, 0);
    nn_assert (cp);

    /*  Remember the type of the socket. */
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    return 0;
}

void nn_usock_stop (struct nn_usock *self)
{
    nn_fsm_stop (&self->fsm);
}

struct nn_fsm *nn_usock_swap_owner (struct nn_usock *self,
    struct nn_fsm *newowner)
{
    return nn_fsm_swap_owner (&self->fsm, newowner);
}

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen)
{
    int rc;

    /*  The socket can be modified only before it's active. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING ||
        self->state == NN_USOCK_STATE_ACCEPTED);

    rc = setsockopt (self->s, level, optname, (char*) optval, optlen);
    if (nn_slow (rc == SOCKET_ERROR))
        return -nn_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    ULONG opt;

    /*  On Windows, the bound port can be hijacked
        if SO_EXCLUSIVEADDRUSE is not set. */
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*) &opt, sizeof (opt));
    wsa_assert (rc != SOCKET_ERROR);

    rc = bind (self->s, addr, addrlen);
    if (nn_slow (rc == SOCKET_ERROR))
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    int rc;

    /*  You can start listening only before the socket is connected. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  Start listening for incoming connections. */
    rc = listen (self->s, backlog);
    if (nn_slow (rc == SOCKET_ERROR))
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    /*  Notify the state machine. */
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_LISTEN);

    return 0;
}

void nn_usock_accept (struct nn_usock *self, struct nn_usock *listener)
{
    int rc;
    BOOL brc;
    DWORD nbytes;

    nn_assert (listener->state == NN_USOCK_STATE_LISTENING);
    nn_assert (self->state = NN_USOCK_STATE_IDLE);
    nn_assert (!self->self);
    
    rc = nn_usock_start (self, listener->domain, listener->type,
        listener->protocol);
    /*  TODO: EMFILE can be returned here. */
    errnum_assert (rc == 0, -rc);
    nn_usock_handler (&listener->fsm, NULL, NN_USOCK_EVENT_ACCEPT);
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_BEING_ACCEPTED);
    
    /*  Wait for the incoming connection. */
    if (!listener->info) {
        listener->info = nn_alloc (512, "accept info");
        alloc_assert (listener->info);
    }
    memset (&listener->in.olpd, 0, sizeof (listener->in.olpd));
    brc = AcceptEx (listener->s, self->s, listener->info, 0, 256, 256, &nbytes,
        &listener->in.olpd);

    /*  Immediate success. */
    if (nn_fast (brc == TRUE)) {
        nn_usock_handler (&listener->fsm, NULL, NN_USOCK_EVENT_DONE);
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_DONE);
        return;
    }

    /*  Immediate failure. */
    if (nn_slow (WSAGetLastError () != WSA_IO_PENDING)) {
        nn_usock_handler (&listener->fsm, NULL, NN_USOCK_EVENT_ERROR);
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ERROR);
        return;
    }

    /*  Asynchronous accept. */
    listener->newsock = self;
    nn_worker_op_start (&listener->in);
}

void nn_usock_activate (struct nn_usock *self)
{
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ACTIVATE);
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    BOOL brc;
    const GUID fid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pconnectex;
    DWORD nbytes;

    /*  Fail if the socket is already connected, closed or such. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  Notify the state machine that we've started connecting. */
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_CONNECT);

    /*  Get the pointer to connect function. */
    rc = WSAIoctl (self->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        (void*) &fid, sizeof (fid), (void*) &pconnectex, sizeof (pconnectex),
        &nbytes, NULL, NULL);
    wsa_assert (rc == 0);
    nn_assert (nbytes == sizeof (pconnectex));

    /*  Connect itself. */
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
    brc = pconnectex (self->s, (struct sockaddr*) addr, addrlen,
        NULL, 0, NULL, &self->out.olpd);
    
    /*  Immediate success. */
    if (nn_fast (brc == TRUE)) {
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_DONE);
        return;
    }

    /*  Immediate error. */
    if (nn_slow (WSAGetLastError () != WSA_IO_PENDING)) {
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ERROR);
        return;
    }

    /*  Asynchronous connect. */
    nn_worker_op_start (&self->out);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iovec *iov,
    int iovcnt)
{
    nn_assert (0);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    nn_assert (0);
}

void nn_usock_handler (struct nn_fsm *self, void *source, int type)
{
    int rc;
    struct nn_usock *usock;

    usock = nn_cont (self, struct nn_usock, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &usock->fsm && type == NN_FSM_STOP)) {

printf ("%p stop\n", usock);

        /*  Socket cannot be stopped while it is being accepted.
            Stop the accepting socket first. */
        nn_assert (usock->state != NN_USOCK_STATE_BEING_ACCEPTED);

        if (usock->state == NN_USOCK_STATE_IDLE)
            goto finish1;
        rc = closesocket (usock->s);
        wsa_assert (rc == 0);
        usock->state = NN_USOCK_STATE_STOPPING;
    } 
    if (nn_slow (usock->state == NN_USOCK_STATE_STOPPING)) {
        if (!nn_worker_op_isidle (&usock->in) ||
              !nn_worker_op_isidle (&usock->out))
            return;
        usock->state = NN_USOCK_STATE_IDLE;
finish1:
        /*  If the socket was closed while accepting new socket
            we'll notify the latter about the fact. */
        if (usock->newsock)
            nn_usock_handler (&usock->newsock->fsm, NULL,
                NN_USOCK_EVENT_ERROR);

printf ("%p stopped\n", usock);
        nn_fsm_stopped (&usock->fsm, usock, NN_USOCK_STOPPED);
        return;
    }

    switch (usock->state) {

/*****************************************************************************/
/*  IDLE state.                                                              */
/*****************************************************************************/
    case NN_USOCK_STATE_IDLE:
        if (source == &usock->fsm) {
            switch (type) {
            case NN_FSM_START:
printf ("%p started\n", usock);
                usock->state = NN_USOCK_STATE_STARTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  STARTING state.                                                          */
/*****************************************************************************/
    case NN_USOCK_STATE_STARTING:
        if (source == NULL) {
            switch (type) {
            case NN_USOCK_EVENT_LISTEN:
printf ("%p listen\n", usock);
                usock->state = NN_USOCK_STATE_LISTENING;
                return;
            case NN_USOCK_EVENT_CONNECT:
printf ("%p connect\n", usock);
                usock->state = NN_USOCK_STATE_CONNECTING;
                return;
            case NN_USOCK_EVENT_BEING_ACCEPTED:
printf ("%p being accepted\n", usock);
                usock->state = NN_USOCK_STATE_BEING_ACCEPTED;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  LISTENING state.                                                         */
/*****************************************************************************/
    case NN_USOCK_STATE_LISTENING:
        if (source == NULL) {
            switch (type) {
            case NN_USOCK_EVENT_ACCEPT:
                usock->state = NN_USOCK_STATE_ACCEPTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  ACCEPTING state.                                                         */
/*****************************************************************************/
    case NN_USOCK_STATE_ACCEPTING:
        nn_assert (0);

/*****************************************************************************/
/*  BEING_ACCEPTED state.                                                    */
/*****************************************************************************/
    case NN_USOCK_STATE_BEING_ACCEPTED:
        if (source == NULL) {
            switch (type) {
            case NN_USOCK_EVENT_DONE:
                nn_assert (0);
            case NN_USOCK_EVENT_ERROR:
printf ("%p: accepting cancelled\n", usock);
                rc = closesocket (usock->s);
                wsa_assert (rc == 0);
                usock->s = INVALID_SOCKET;
                usock->state = NN_USOCK_STATE_IDLE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  CONNECTING state.                                                        */
/*****************************************************************************/
    case NN_USOCK_STATE_CONNECTING:
        if (source == NULL) {
            switch (type) {
            case NN_USOCK_EVENT_DONE:
                nn_assert (0);
            case NN_USOCK_EVENT_ERROR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        if (source == &usock->out) {
            switch (type) {
            case NN_WORKER_OP_DONE:
                nn_assert (0);
            case NN_WORKER_OP_ERROR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  DONE state.                                                              */
/*****************************************************************************/
    case NN_USOCK_STATE_DONE:
        nn_assert (0);

/*****************************************************************************/
/*  Invalid state.                                                           */
/*****************************************************************************/
    default:
        nn_assert (0);
    }
}
