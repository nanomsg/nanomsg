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
#include "err.h"

/*  Private functions. */
static void sp_usock_tune (struct sp_usock *self);

void sp_timer_init (struct sp_timer *self, const struct sp_sink **sink,
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

    /*  If the timer is active, cancel it first. */
    if (self->active)
        sp_timer_stop (self);

    self->active = 1;
    rc = sp_timeout_add (&self->cp->timeout, timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);

    if (rc == 1 && !sp_thread_current (&self->cp->worker))
        /* TODO: Interrupt the worker thread. */;
}

void sp_timer_stop (struct sp_timer *self)
{
    int rc;

    /*  If the timer is not active, do nothing. */
    if (!self->active)
         return;

    rc = sp_timeout_rm (&self->cp->timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->cp->worker))
        /* TODO: Interrupt the worker thread. */;
}

int sp_usock_init (struct sp_usock *self, const struct sp_sink **sink,
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

const struct sp_sink **sp_usock_setsink (struct sp_usock *self,
    const struct sp_sink **sink)
{
    const struct sp_sink **original;

    original = self->sink;
    self->sink = sink;
    return original;
}

int sp_usock_init_child (struct sp_usock *self, struct sp_usock *parent,
    int s, const struct sp_sink **sink, struct sp_cp *cp)
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
    u_long flags;
    DWORD only;
#if defined HANDLE_FLAG_INHERIT
    BOOL brc;
#endif

    /*  Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the asynchronous mode. */
    flags = 1;
    rc = ioctlsocket (self->s, FIONBIO, &flags);
    wsa_assert (rc != SOCKET_ERROR);

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

void sp_cp_init (struct sp_cp *self, const struct sp_cp_vfptr *vfptr)
{
    /*  Create system-level completion port. */
    self->hndl = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self->hndl);

    /*  Launch the worker thread. */
    self->stop = 0;
    sp_thread_init (&self->worker, sp_cp_worker, self);
}

void sp_cp_term (struct sp_cp *self)
{
    BOOL brc;

    brc = CloseHandle (self->hndl);
    win_assert (brc);
}

void sp_cp_lock (struct sp_cp *self)
{
    sp_mutex_lock (&self->sync);
}

void sp_cp_unlock (struct sp_cp *self)
{
    sp_mutex_unlock (&self->sync);
}

void sp_cp_post (struct sp_cp *self, int op, struct sp_event *event)
{
    BOOL brc;

    brc = PostQueuedCompletionStatus (self->hndl, (DWORD) op,
        (ULONG_PTR) event, NULL);
    win_assert (brc);
}

static void sp_cp_worker (void *arg)
{
    struct sp_cp *self;
    int timeout;
    BOOL brc;
    DWORD nbytes;
    ULONG_PTR key;
    LPOVERLAPPED olpd;

    self = (struct sp_cp*) arg;

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = sp_timeout_timeout (&self->timeout);

        /*  Wait for new events and/or timeouts. */
        sp_mutex_unlock (&self->sync);
        brc = GetQueuedCompletionStatus (self->hndl, &nbytes, &key,
            &olpd, timeout < 0 ? INFINITE : timeout);
        sp_mutex_lock (&self->sync);

        if (sp_slow (!brc && !olpd)) {
            /*  TODO: Timeout. */
            sp_assert (0);
        }
        win_assert (brc);

        /*  TODO */
        sp_assert (0);
    }
}

void sp_usock_term (struct sp_usock *self)
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
    const GUID fid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pconnectex;
    DWORD nbytes;

    rc = WSAIoctl (self->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        (void*) &fid, sizeof (fid), (void*) &pconnectex, sizeof (pconnectex),
        &nbytes, NULL, NULL);
    wsa_assert (rc == 0);
    sp_assert (nbytes == sizeof (pconnectex));
    memset (&self->conn, 0, sizeof (self->conn));
    brc = pconnectex (self->s, (struct sockaddr*) &addr, addrlen,
        NULL, 0, NULL, (OVERLAPPED*) &self->conn);
    if (sp_fast (brc == TRUE)) {
        sp_assert ((*self->sink)->connected);
        (*self->sink)->connected (self->sink, self);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void sp_usock_accept (struct sp_usock *self)
{
    BOOL brc;
    char info [64];
    DWORD nbytes;

    /*  TODO: Create newsock here. */
    sp_assert (0);

    brc = AcceptEx (self->s, self->newsock, info, 0, 256, 256, &nbytes,
        &self->in);
    if (sp_fast (brc == TRUE)) {
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
    DWORD nbytes;

    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    memset (&self->out, 0, sizeof (self->out));
    rc = WSASend (self->s, &wbuf, 1, &nbytes, 0, &self->out, NULL);
    if (sp_fast (rc == 0)) {
        sp_assert (nbytes == len);
        sp_assert ((*self->sink)->sent);
        (*self->sink)->sent (self->sink, self, nbytes);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

void sp_usock_recv (struct sp_usock *self, void *buf, size_t len)
{
    int rc;
    WSABUF wbuf;
    DWORD wflags;
    DWORD nbytes;

    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    wflags = MSG_WAITALL;
    memset (&self->in, 0, sizeof (self->in));
    rc = WSARecv (self->s, &wbuf, 1, &nbytes, &wflags, &self->in, NULL);
    if (sp_fast (rc == 0)) {
        sp_assert (nbytes == len);
        sp_assert ((*self->sink)->received);
        (*self->sink)->received (self->sink, self, nbytes);
        return;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
}

