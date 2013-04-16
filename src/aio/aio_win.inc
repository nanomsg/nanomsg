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

#include "aio.h"

#include "../utils/win.h"
#include "../utils/cont.h"
#include "../utils/fast.h"
#include "../utils/err.h"

/*  Private functions. */
static void nn_usock_tune (struct nn_usock *self, int sndbuf, int rcvbuf);
static void nn_cp_worker (void *arg);

void nn_timer_init (struct nn_timer *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    nn_timerset_hndl_init (&self->hndl);
}

void nn_timer_term (struct nn_timer *self)
{
    nn_timer_stop (self);
    nn_timerset_hndl_term (&self->hndl);
}

void nn_timer_start (struct nn_timer *self, int timeout)
{
    int rc;
    BOOL brc;

    /*  If the timer is active, cancel it first. */
    if (nn_timerset_hndl_isactive (&self->hndl))
        nn_timer_stop (self);

    rc = nn_timerset_add (&self->cp->timeout, timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);

    if (rc == 1 && !nn_thread_current (&self->cp->worker)) {
        brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) &self->cp->timer_event, NULL);
        win_assert (brc);
    }
}

void nn_timer_stop (struct nn_timer *self)
{
    int rc;
    BOOL brc;

    /*  If the timer is not active, do nothing. */
    if (!nn_timerset_hndl_isactive (&self->hndl))
         return;

    rc = nn_timerset_rm (&self->cp->timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !nn_thread_current (&self->cp->worker)) {
        brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) &self->cp->timer_event, NULL);
        win_assert (brc);
    }
}

void nn_event_init (struct nn_event *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    self->active = 0;
}

void nn_event_term (struct nn_event *self)
{
    nn_assert (!self->active);
}

void nn_event_signal (struct nn_event *self)
{
    BOOL brc;

    self->active = 1;
    brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) self, NULL);
    win_assert (brc);
}

int nn_usock_init (struct nn_usock *self, const struct nn_cp_sink **sink,
    int domain, int type, int protocol, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
    HANDLE wcp;

    self->sink = sink;
    self->cp = cp;
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;

    /*  Open the underlying socket. */
    self->s = socket (domain, type, protocol);
    if (self->s == INVALID_SOCKET)
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    nn_usock_tune (self, sndbuf, rcvbuf);

    /*  On Windows platform, socket is assocaited with a completion port
        immediately after creation. */
    wcp = CreateIoCompletionPort ((HANDLE) self->s, cp->hndl,
        (ULONG_PTR) NULL, 0);
    nn_assert (wcp);

    return 0;
}

const struct nn_cp_sink **nn_usock_setsink (struct nn_usock *self,
    const struct nn_cp_sink **sink)
{
    const struct nn_cp_sink **original;

    original = self->sink;
    self->sink = sink;
    return original;
}

int nn_usock_init_child (struct nn_usock *self, struct nn_usock *parent,
    int s, const struct nn_cp_sink **sink, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->s = s;
    self->cp = cp;
    self->domain = parent->domain;
    self->type = parent->type;
    self->protocol = parent->protocol;

    nn_usock_tune (self, sndbuf, rcvbuf);

    return 0;
}

static void nn_usock_tune (struct nn_usock *self, int sndbuf, int rcvbuf)
{
    int rc;
    int opt;
    DWORD only;
#if defined HANDLE_FLAG_INHERIT
    BOOL brc;
#endif

    /*  Set the size of tx and rc buffers. */
    if (sndbuf >= 0) {
        rc = setsockopt (self->s, SOL_SOCKET, SO_SNDBUF,
            (char*) &sndbuf, sizeof (sndbuf));
        wsa_assert (rc != SOCKET_ERROR);
    }
    if (rcvbuf >= 0) {
        rc = setsockopt (self->s, SOL_SOCKET, SO_RCVBUF,
            (char*) &rcvbuf, sizeof (rcvbuf));
        wsa_assert (rc != SOCKET_ERROR);
    }

    /*  On TCP sockets switch off the Nagle's algorithm to get
        the best possible latency. */
    if ((self->domain == AF_INET || self->domain == AF_INET6) &&
          self->type == SOCK_STREAM) {
        opt = 1;
        rc = setsockopt (self->s, IPPROTO_TCP, TCP_NODELAY,
            (const char*) &opt, sizeof (opt));
        wsa_assert (rc != SOCKET_ERROR);
    }

    /*  On some operating systems IPv4 mapping for IPv6 sockets is disabled
        by default. In such case, switch it on. */
#if defined IPV6_V6ONLY
    if (self->domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->s, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char*) &only, sizeof (only));
        wsa_assert (rc != SOCKET_ERROR);
    }
#endif

    /*  Disable inheriting the socket to the child processes. */
#if defined HANDLE_FLAG_INHERIT
    brc = SetHandleInformation ((HANDLE) self->s, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);
#endif
}

int nn_cp_init (struct nn_cp *self)
{
    nn_mutex_init (&self->sync);
    nn_timerset_init (&self->timeout);

    /*  Create system-level completion port. */
    self->hndl = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self->hndl);

    /*  Launch the worker thread. */
    nn_thread_init (&self->worker, nn_cp_worker, self);

    return 0;
}

void nn_cp_term (struct nn_cp *self)
{
    BOOL brc;

    /*  Ask worker thread to terminate. */
    brc = PostQueuedCompletionStatus (self->hndl, 0,
        (ULONG_PTR) &self->stop_event, NULL);
    win_assert (brc);

    /*  Wait till it terminates. */
    nn_thread_term (&self->worker);

    /*  TODO: Cancel any pending operations
        (unless closing CP terminates them automatically). */

    /*  Deallocate the resources. */
    brc = CloseHandle (self->hndl);
    win_assert (brc);
    nn_timerset_term (&self->timeout);
    nn_mutex_term (&self->sync);
}

void nn_cp_lock (struct nn_cp *self)
{
    nn_mutex_lock (&self->sync);
}

void nn_cp_unlock (struct nn_cp *self)
{
    nn_mutex_unlock (&self->sync);
}

static void nn_cp_worker (void *arg)
{
    int rc;
    struct nn_cp *self;
    int timeout;
    BOOL brc;
    DWORD nbytes;
    ULONG_PTR key;
    LPOVERLAPPED olpd;
    struct nn_timerset_hndl *tohndl;
    struct nn_timer *timer;
    struct nn_event *event;
    struct nn_usock_op *op;
    struct nn_usock *usock;

    self = (struct nn_cp*) arg;

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = nn_timerset_timeout (&self->timeout);

        /*  Wait for new events and/or timeouts. */
        /*  TODO: In theory we may gain some performance by getting multiple
            events at once via GetQueuedCompletionStatusEx function. */
        nn_mutex_unlock (&self->sync);
        brc = GetQueuedCompletionStatus (self->hndl, &nbytes, &key,
            &olpd, timeout < 0 ? INFINITE : timeout);
        nn_mutex_lock (&self->sync);

        /*  If there's an error that is not an timeout, fail. */
        win_assert (brc || !olpd);

        /*  Process any expired timers. */
        while (1) {
            rc = nn_timerset_event (&self->timeout, &tohndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  Fire the timeout event. */
            timer = nn_cont (tohndl, struct nn_timer, hndl);
            nn_assert ((*timer->sink)->timeout);
            (*timer->sink)->timeout (timer->sink, timer);
        }

        /*  Timer event requires no processing. Its sole intent is to
            interrupt the polling in the worker thread. */
        if (nn_slow ((char*) key == &self->timer_event))
            continue;

        /*  Completion port shutdown is underway. Exit the worker thread. */
        if (nn_slow ((char*) key == &self->stop_event))
            break;

        /*  Custom events are reported via callback. */
        if (key) {
            event = (struct nn_event*) key;
            nn_assert ((*event->sink)->event);
            (*event->sink)->event (event->sink, event);
            event->active = 0;
            continue;
        }
        
        /*  I/O completion events */
        nn_assert (olpd);
        op = nn_cont (olpd, struct nn_usock_op, olpd);
        switch (op->op) {
        case NN_USOCK_OP_RECV:
            usock = nn_cont (op, struct nn_usock, in);
            nn_assert ((*usock->sink)->received);
printf ("received olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->received (usock->sink, usock);
            break;
        case NN_USOCK_OP_SEND:
            usock = nn_cont (op, struct nn_usock, out);
            nn_assert ((*usock->sink)->sent);
printf ("sent olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->sent (usock->sink, usock);
            break;
        case NN_USOCK_OP_CONNECT:
            usock = nn_cont (op, struct nn_usock, out);
            nn_assert ((*usock->sink)->connected);
printf ("connected olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->connected (usock->sink, usock);
            break;
        case NN_USOCK_OP_ACCEPT:
            usock = nn_cont (op, struct nn_usock, in);
            nn_assert ((*usock->sink)->accepted);
printf ("accepted olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->accepted (usock->sink, usock, usock->newsock);
            break;
        case NN_USOCK_OP_CONN:
            usock = nn_cont (op, struct nn_usock, conn);
            nn_assert (0);
        default:
            nn_assert (0);
        }
    }
}

void nn_usock_close (struct nn_usock *self)
{
    int rc;

    rc = closesocket (self->s);
    wsa_assert (rc != SOCKET_ERROR);
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen)
{
    int rc;
    int opt;

    /*  On Windows, the bound port can be hijacked if SO_EXCLUSIVEADDRUSE
        is not set. */
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

    rc = listen (self->s, backlog);
    wsa_assert (rc != SOCKET_ERROR);

    return 0;
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen)
{
    int rc;
    BOOL brc;
    struct sockaddr_in baddr;
    const GUID fid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pconnectex;
    DWORD nbytes;

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

    rc = WSAIoctl (self->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        (void*) &fid, sizeof (fid), (void*) &pconnectex, sizeof (pconnectex),
        &nbytes, NULL, NULL);
    wsa_assert (rc == 0);
    nn_assert (nbytes == sizeof (pconnectex));
    self->out.op = NN_USOCK_OP_CONNECT;
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
printf ("nn_usock_connect olpd=%p\n", (void*) &self->out.olpd);
    brc = pconnectex (self->s, (struct sockaddr*) addr, addrlen,
        NULL, 0, NULL, &self->out.olpd);
    /*  TODO: It's not clear what happens when connect is immediately
        successful. Is the completion reported in async way anyway? */
    if (nn_fast (brc == TRUE)) {
printf ("connected immediately olpd=%p\n", (void*) &self->out.olpd);
        nn_assert ((*self->sink)->connected);
        (*self->sink)->connected (self->sink, self);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void nn_usock_accept (struct nn_usock *self)
{
    BOOL brc;
    char info [512];
    DWORD nbytes;
    HANDLE wcp;

    /*  Open new socket and associate it with the completion port. */
    self->newsock = socket (self->domain, self->type, self->protocol);
    wsa_assert (self->newsock != INVALID_SOCKET);
    wcp = CreateIoCompletionPort ((HANDLE) self->newsock, self->cp->hndl,
        (ULONG_PTR) NULL, 0);
    nn_assert (wcp);

    /*  Asynchronously wait for new incoming connection. */
    self->in.op = NN_USOCK_OP_ACCEPT;
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
printf ("nn_usock_accept olpd=%p\n", (void*) &self->in.olpd);
    brc = AcceptEx (self->s, self->newsock, info, 0, 256, 256, &nbytes,
        &self->in.olpd);
    /*  TODO: It's not clear what happens when accept is immediately
        successful. Is the completion reported in async way anyway? */
    if (nn_fast (brc == TRUE)) {
printf ("accepted immediately olpd=%p\n", (void*) &self->in.olpd);
        nn_assert ((*self->sink)->accepted);
        (*self->sink)->accepted (self->sink, self, self->newsock);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iobuf *iov,
    int iovcnt)
{
    int rc;
    WSABUF wbuf [NN_AIO_MAX_IOVCNT];
    int i;

    /*  Create an WinAPI compliant iovec. */
    nn_assert (iovcnt <= NN_AIO_MAX_IOVCNT);
    for (i = 0; i != iovcnt; ++i) {
        wbuf [i].buf = (char FAR*) iov [i].iov_base;
        wbuf [i].len = (u_long) iov [i].iov_len;
    }

    /*  Start the send oprtation. */
    self->out.op = NN_USOCK_OP_SEND;
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
printf ("nn_usock_send olpd=%p\n", (void*) &self->out.olpd);
    rc = WSASend (self->s, wbuf, iovcnt, NULL, 0, &self->out.olpd, NULL);
    wsa_assert (rc == 0 || WSAGetLastError () == WSA_IO_PENDING);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    int rc;
    WSABUF wbuf;
    DWORD wflags;

    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    wflags = MSG_WAITALL;
    self->in.op = NN_USOCK_OP_RECV;
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
printf ("nn_usock_recv olpd=%p size=%d\n", (void*) &self->in.olpd, (int) len);
    rc = WSARecv (self->s, &wbuf, 1, NULL, &wflags, &self->in.olpd, NULL);
    wsa_assert (rc == 0 || WSAGetLastError () == WSA_IO_PENDING);
}
