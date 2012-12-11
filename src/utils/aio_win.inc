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
#include "win.h"
#include "cont.h"
#include "fast.h"
#include "err.h"

/*  Private functions. */
static void sp_usock_tune (struct sp_usock *self);
static void sp_cp_worker (void *arg);

void sp_timer_init (struct sp_timer *self, const struct sp_cp_sink **sink,
    struct sp_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    self->active = 0;
}

void sp_timer_term (struct sp_timer *self)
{
    sp_timer_stop (self);
}

void sp_timer_start (struct sp_timer *self, int timeout)
{
    int rc;
    BOOL brc;

    /*  If the timer is active, cancel it first. */
    if (self->active)
        sp_timer_stop (self);

    self->active = 1;
    rc = sp_timeout_add (&self->cp->timeout, timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);

    if (rc == 1 && !sp_thread_current (&self->cp->worker)) {
        brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) &self->cp->timer_event, NULL);
        win_assert (brc);
    }
}

void sp_timer_stop (struct sp_timer *self)
{
    int rc;
    BOOL brc;

    /*  If the timer is not active, do nothing. */
    if (!self->active)
         return;

    rc = sp_timeout_rm (&self->cp->timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->cp->worker)) {
        brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) &self->cp->timer_event, NULL);
        win_assert (brc);
    }
}

void sp_event_init (struct sp_event *self, const struct sp_cp_sink **sink,
    struct sp_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    self->active = 0;
}

void sp_event_term (struct sp_event *self)
{
    sp_assert (!self->active);
}

void sp_event_signal (struct sp_event *self)
{
    BOOL brc;

    brc = PostQueuedCompletionStatus (self->cp->hndl, 0,
            (ULONG_PTR) self, NULL);
    win_assert (brc);
}

int sp_usock_init (struct sp_usock *self, const struct sp_cp_sink **sink,
    int domain, int type, int protocol, struct sp_cp *cp)
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
       return -sp_err_wsa_to_posix (WSAGetLastError ());

    sp_usock_tune (self);

    /*  On Windows platform, socket is assocaited with a completion port
        immediately after creation. */
    wcp = CreateIoCompletionPort ((HANDLE) self->s, cp->hndl,
        (ULONG_PTR) NULL, 0);
    sp_assert (wcp);

    return 0;
}

const struct sp_cp_sink **sp_usock_setsink (struct sp_usock *self,
    const struct sp_cp_sink **sink)
{
    const struct sp_cp_sink **original;

    original = self->sink;
    self->sink = sink;
    return original;
}

int sp_usock_init_child (struct sp_usock *self, struct sp_usock *parent,
    int s, const struct sp_cp_sink **sink, struct sp_cp *cp)
{
    self->sink = sink;
    self->s = s;
    self->cp = cp;
    self->domain = parent->domain;
    self->type = parent->type;
    self->protocol = parent->protocol;

    sp_usock_tune (self);

    return 0;
}

static void sp_usock_tune (struct sp_usock *self)
{
    int rc;
    int opt;
    DWORD only;
#if defined HANDLE_FLAG_INHERIT
    BOOL brc;
#endif

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

void sp_cp_init (struct sp_cp *self)
{
    sp_mutex_init (&self->sync);
    sp_timeout_init (&self->timeout);

    /*  Create system-level completion port. */
    self->hndl = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self->hndl);

    /*  Launch the worker thread. */
    sp_thread_init (&self->worker, sp_cp_worker, self);
}

void sp_cp_term (struct sp_cp *self)
{
    BOOL brc;

    /*  Ask worker thread to terminate. */
    brc = PostQueuedCompletionStatus (self->hndl, 0,
        (ULONG_PTR) &self->stop_event, NULL);
    win_assert (brc);

    /*  Wait till it terminates. */
    sp_thread_term (&self->worker);

    /*  TODO: Cancel any pending operations
        (unless closing CP terminates them automatically). */

    /*  Deallocate the resources. */
    brc = CloseHandle (self->hndl);
    win_assert (brc);
    sp_timeout_term (&self->timeout);
    sp_mutex_term (&self->sync);
}

void sp_cp_lock (struct sp_cp *self)
{
    sp_mutex_lock (&self->sync);
}

void sp_cp_unlock (struct sp_cp *self)
{
    sp_mutex_unlock (&self->sync);
}

static void sp_cp_worker (void *arg)
{
    int rc;
    struct sp_cp *self;
    int timeout;
    BOOL brc;
    DWORD nbytes;
    ULONG_PTR key;
    LPOVERLAPPED olpd;
    struct sp_timeout_hndl *tohndl;
    struct sp_timer *timer;
    struct sp_event *event;
    struct sp_usock_op *op;
    struct sp_usock *usock;

    self = (struct sp_cp*) arg;

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = sp_timeout_timeout (&self->timeout);

        /*  Wait for new events and/or timeouts. */
        /*  TODO: In theory we may gain some performance by getting multiple
            events at once via GetQueuedCompletionStatusEx function. */
        sp_mutex_unlock (&self->sync);
        brc = GetQueuedCompletionStatus (self->hndl, &nbytes, &key,
            &olpd, timeout < 0 ? INFINITE : timeout);
        sp_mutex_lock (&self->sync);

        /*  If there's an error that is not an timeout, fail. */
        win_assert (brc || !olpd);

        /*  Process any expired timers. */
        while (1) {
            rc = sp_timeout_event (&self->timeout, &tohndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  Fire the timeout event. */
            timer = sp_cont (tohndl, struct sp_timer, hndl);
            sp_assert ((*timer->sink)->timeout);
            (*timer->sink)->timeout (timer->sink, timer);
        }

        /*  Timer event requires no processing. Its sole intent is to
            interrupt the polling in the worker thread. */
        if (sp_slow ((char*) key == &self->timer_event))
            continue;

        /*  Completion port shutdown is underway. Exit the worker thread. */
        if (sp_slow ((char*) key == &self->stop_event))
            break;

        /*  Custom events are reported via callback. */
        if (key) {
            event = (struct sp_event*) key;
            sp_assert ((*event->sink)->event);
            (*event->sink)->event (event->sink, event);
            event->active = 0;
            continue;
        }
        
        /*  I/O completion events */
        sp_assert (olpd);
        op = sp_cont (olpd, struct sp_usock_op, olpd);
        switch (op->op) {
        case SP_USOCK_OP_RECV:
            usock = sp_cont (op, struct sp_usock, in);
            sp_assert ((*usock->sink)->received);
printf ("received olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->received (usock->sink, usock);
            break;
        case SP_USOCK_OP_SEND:
            usock = sp_cont (op, struct sp_usock, out);
            sp_assert ((*usock->sink)->sent);
printf ("sent olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->sent (usock->sink, usock);
            break;
        case SP_USOCK_OP_CONNECT:
            usock = sp_cont (op, struct sp_usock, out);
            sp_assert ((*usock->sink)->connected);
printf ("connected olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->connected (usock->sink, usock);
            break;
        case SP_USOCK_OP_ACCEPT:
            usock = sp_cont (op, struct sp_usock, in);
            sp_assert ((*usock->sink)->accepted);
printf ("accepted olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->accepted (usock->sink, usock, usock->newsock);
            break;
        case SP_USOCK_OP_CONN:
            usock = sp_cont (op, struct sp_usock, conn);
            sp_assert (0);
        default:
            sp_assert (0);
        }
    }
}

void sp_usock_close (struct sp_usock *self)
{
    int rc;

    rc = closesocket (self->s);
    wsa_assert (rc != SOCKET_ERROR);
}

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
{
    int rc;

    rc = bind (self->s, addr, addrlen);
    if (sp_slow (rc == SOCKET_ERROR))
       return -sp_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int sp_usock_listen (struct sp_usock *self, int backlog)
{
    int rc;
    int opt;

    /*  On Windows, the bound port can be hijacked if SO_EXCLUSIVEADDRUSE
        is not set. */
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*) &opt, sizeof (opt));
    wsa_assert (rc != SOCKET_ERROR);

    rc = listen (self->s, backlog);
    wsa_assert (rc != SOCKET_ERROR);

    return 0;
}

void sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
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
    sp_assert (addr->sa_family == AF_INET);
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
    sp_assert (nbytes == sizeof (pconnectex));
    self->out.op = SP_USOCK_OP_CONNECT;
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
printf ("sp_usock_connect olpd=%p\n", (void*) &self->out.olpd);
    brc = pconnectex (self->s, (struct sockaddr*) addr, addrlen,
        NULL, 0, NULL, &self->out.olpd);
    /*  TODO: It's not clear what happens when connect is immediately
        successful. Is the completion reported in async way anyway? */
    if (sp_fast (brc == TRUE)) {
printf ("connected immediately olpd=%p\n", (void*) &self->out.olpd);
        sp_assert ((*self->sink)->connected);
        (*self->sink)->connected (self->sink, self);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void sp_usock_accept (struct sp_usock *self)
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
    sp_assert (wcp);

    /*  Asynchronously wait for new incoming connection. */
    self->in.op = SP_USOCK_OP_ACCEPT;
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
printf ("sp_usock_accept olpd=%p\n", (void*) &self->in.olpd);
    brc = AcceptEx (self->s, self->newsock, info, 0, 256, 256, &nbytes,
        &self->in.olpd);
    /*  TODO: It's not clear what happens when accept is immediately
        successful. Is the completion reported in async way anyway? */
    if (sp_fast (brc == TRUE)) {
printf ("accepted immediately olpd=%p\n", (void*) &self->in.olpd);
        sp_assert ((*self->sink)->accepted);
        (*self->sink)->accepted (self->sink, self, self->newsock);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void sp_usock_send (struct sp_usock *self, const void *buf, size_t len)
{
    int rc;
    WSABUF wbuf;

    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    self->out.op = SP_USOCK_OP_SEND;
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
printf ("sp_usock_send olpd=%p size=%d\n", (void*) &self->out.olpd, (int) len);
    rc = WSASend (self->s, &wbuf, 1, NULL, 0, &self->out.olpd, NULL);
    wsa_assert (rc == 0 || WSAGetLastError () == WSA_IO_PENDING);
}

void sp_usock_recv (struct sp_usock *self, void *buf, size_t len)
{
    int rc;
    WSABUF wbuf;
    DWORD wflags;

    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    wflags = MSG_WAITALL;
    self->in.op = SP_USOCK_OP_RECV;
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
printf ("sp_usock_recv olpd=%p size=%d\n", (void*) &self->in.olpd, (int) len);
    rc = WSARecv (self->s, &wbuf, 1, NULL, &wflags, &self->in.olpd, NULL);
    wsa_assert (rc == 0 || WSAGetLastError () == WSA_IO_PENDING);
}
