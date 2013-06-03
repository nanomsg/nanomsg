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

#include <stddef.h>

#define NN_USOCK_STATE_IDLE 1
#define NN_USOCK_STATE_STARTING 2
#define NN_USOCK_STATE_LISTENING 3
#define NN_USOCK_STATE_ACCEPTING 4
#define NN_USOCK_STATE_BEING_ACCEPTED 5

#define NN_USOCK_EVENT_LISTEN 1
#define NN_USOCK_EVENT_CONNECTED 2
#define NN_USOCK_EVENT_CONNECTING 3
#define NN_USOCK_EVENT_ACCEPTED 4
#define NN_USOCK_EVENT_ACCEPTING 4
#define NN_USOCK_EVENT_ERROR 5

/*  Private functions. */
void nn_usock_handler (struct nn_fsm *self, void *source, int type);

void nn_usock_init (struct nn_usock *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_usock_handler, owner);
    self->state = NN_USOCK_STATE_IDLE;
    self->s = INVALID_SOCKET;
    self->in.usock = self;
    self->out.usock = self;
    self->domain = -1;
    self->type = -1;
    self->protocol = -1;
    self->newsock = NULL;
}

void nn_usock_term (struct nn_usock *self)
{
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
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    rc = setsockopt (self->s, level, optname, (char*) optval, optlen);
    if (nn_slow (rc == SOCKET_ERROR))
        return -nn_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    int opt;

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

printf ("%p: listen\n", self);

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

void nn_usock_accept (struct nn_usock *self, struct nn_usock *newsock)
{
    int rc;
    BOOL brc;
    char info [512];
    DWORD nbytes;

printf ("%p: accept => %p\n", self, newsock);

    /*  Socket must be listening to accept new connections. */
    nn_assert (self->state == NN_USOCK_STATE_LISTENING);

    /*  Open the new undelying socket. */
    rc = nn_usock_start (newsock, self->domain, self->type, self->protocol);
    errnum_assert (rc == 0, -rc);

    /*  Wait for the incoming connection. */
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
    brc = AcceptEx (self->s, self->newsock->s, info, 0, 256, 256, &nbytes,
        &self->in.olpd);

    /*  Immediate success. */
    if (nn_fast (brc == TRUE)) {
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ACCEPTED);
        return;
    }

    /*  Immediate failure. */
    if (nn_slow (WSAGetLastError () != WSA_IO_PENDING)) {
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ERROR);
        return;
    }

    /*  Asynchronous accept. */
    self->newsock = newsock;
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ACCEPTING);
}

void nn_usock_activate (struct nn_usock *self)
{
    nn_assert (0);
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    BOOL brc;
//    struct sockaddr_in baddr;
    const GUID fid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pconnectex;
    DWORD nbytes;

    /*  Fail if the socket is already connected, closed or such. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  TODO: Binding to local interface is mandatory with IOCP! */
#if 0
    /*  Set local address for the connection. */
    /*  TODO: User should be able to specify the address. */
    /*  TODO: What about IPv6? If so, we should bind to in6addr_any. */
    nn_assert (addr->sa_family == AF_INET);
    memset (&baddr, 0, sizeof (baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port = htons (0);
    baddr.sin_addr.s_addr = INADDR_ANY;
    rc = bind (self->s, (struct sockaddr*) &baddr, sizeof (baddr));
    wsa_assert (rc == 0);
#endif

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
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_CONNECTED);
        return;
    }

    /*  Immediate error. */
    if (nn_slow (WSAGetLastError () != WSA_IO_PENDING)) {
        nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_ERROR);
        return;
    }

    /*  Asynchronous connect. */
    nn_usock_handler (&self->fsm, NULL, NN_USOCK_EVENT_CONNECTING);
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
    struct nn_usock *usock;

    usock = nn_cont (self, struct nn_usock, fsm);

printf ("%p: event source=%p type=%d\n", usock, source, type);

    switch (usock->state) {

/*****************************************************************************/
/*  IDLE state.                                                              */
/*****************************************************************************/
    case NN_USOCK_STATE_IDLE:
        if (source == &usock->fsm) {
            switch (type) {
            case NN_FSM_START:
printf ("%p: starting\n", usock);
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
                usock->state = NN_USOCK_STATE_LISTENING;
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
            case NN_USOCK_EVENT_LISTEN:
                usock->state = NN_USOCK_STATE_ACCEPTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  Invalid state.                                                           */
/*****************************************************************************/
    default:
        nn_assert (0);
    }
}