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

#include "usock.h"
#include "err.h"

#if !defined SP_HAVE_WINDOWS
#if defined SP_HAVE_ACCEPT4
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

/*  Private functions. */
static void sp_usock_tune (struct sp_usock *self);

int sp_usock_init (struct sp_usock *self, int domain, int type, int protocol,
    struct sp_cp *cp)
{
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    int rc;
#endif
#if defined SP_HAVE_WINDOWS
    HANDLE wcp;
#endif

    /*  If the operating system allows to directly open the socket with CLOEXEC
        flag, do so. That way there are no race conditions. */
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif

    /*  Open the underlying socket. */
    self->hndl.s = socket (domain, type, protocol);
#if defined SP_HAVE_WINDOWS
    if (self->hndl.s == INVALID_SOCKET)
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (self->hndl.s < 0)
       return -errno;
#endif
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;
    self->cp = cp;

    /*  Setting FD_CLOEXEC option immediately after socket creation is the
        second best option. There is a race condition (if process is forked
        between socket creation and setting the option) but the problem is
        pretty unlikely to happen. */
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    rc = fcntl (self->hndl.s, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    sp_usock_tune (self);

#if defined SP_HAVE_WINDOWS
    wcp = CreateIoCompletionPort ((HANDLE) self->hndl.s, cp->hndl,
        (ULONG_PTR) NULL, 0);
    sp_assert (wcp);
#endif

    return 0;
}

static void sp_usock_tune (struct sp_usock *self)
{
    int rc;
    int opt;
#if defined SP_HAVE_WINDOWS
    u_long flags;
    BOOL brc;
    DWORD only;
#else
    int flags;
    int only;
#endif

    /*  If applicable, prevent SIGPIPE signal when writing to the connection
        already closed by the peer. */
#ifdef SO_NOSIGPIPE
    opt = 1;
    rc = setsockopt (self->hndl.s, SOL_SOCKET, SO_NOSIGPIPE,
        &opt, sizeof (opt));
    errno_assert (rc == 0);
#endif

    /*  Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the asynchronous mode. */
#if defined SP_HAVE_WINDOWS
    flags = 1;
    rc = ioctlsocket (self->hndl.s, FIONBIO, &flags);
    wsa_assert (rc != SOCKET_ERROR);
#else
	flags = fcntl (self->hndl.s, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	rc = fcntl (self->hndl.s, F_SETFL, flags | O_NONBLOCK);
    errno_assert (rc != -1);
#endif

    /*  On TCP sockets switch off the Nagle's algorithm to get
        the best possible latency. */
    if ((self->domain == AF_INET || self->domain == AF_INET6) &&
          self->type == SOCK_STREAM) {
        opt = 1;
        rc = setsockopt (self->hndl.s, IPPROTO_TCP, TCP_NODELAY,
            (const char*) &opt, sizeof (opt));
#if defined SP_HAVE_WINDOWS
        wsa_assert (rc != SOCKET_ERROR);
#else
        errno_assert (rc == 0);
#endif
    }

    /*  If applicable, disable delayed acknowledgements to improve latency. */
#if defined TCP_NODELACK
    opt = 1;
    rc = setsockopt (self->hndl.s, IPPROTO_TCP, TCP_NODELACK,
        &opt, sizeof (opt));
    errno_assert (rc == 0);
#endif

    /*  On some operating systems IPv4 mapping for IPv6 sockets is disabled
        by default. In such case, switch it on. */
#if defined IPV6_V6ONLY
    if (self->domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->hndl.s, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char*) &only, sizeof (only));
#ifdef SP_HAVE_WINDOWS
        wsa_assert (rc != SOCKET_ERROR);
#else
        errno_assert (rc == 0);
#endif
    }
#endif

/*  On Windows, disable inheriting the socket to the child processes. */
#if defined SP_HAVE_WINDOWS && defined HANDLE_FLAG_INHERIT
    brc = SetHandleInformation ((HANDLE) self->hndl.s, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);
#endif
}

#if defined SP_HAVE_WINDOWS
#error
#else

void sp_usock_term (struct sp_usock *self)
{
    int rc;

    /*  Stop polling on the socket and close it. */
    /*  TODO: What if term is called before connect or listen? */
    sp_cp_rm_fd (self->cp, &self->hndl);
    rc = close (self->hndl.s);
    errno_assert (rc == 0);
}

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
{
    int rc;

    rc = bind (self->hndl.s, addr, addrlen);
    if (sp_slow (rc < 0))
       return -errno;

    return 0;
}

int sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
{
    sp_assert (0);
}

int sp_usock_listen (struct sp_usock *self, int backlog)
{
    int rc;
    int opt;

    /*  To allow for rapid restart of SP services, allow new bind to succeed
        immediately after previous instance of the process failed, skipping the
        grace period. */
    opt = 1;
    rc = setsockopt (self->hndl.s, SOL_SOCKET, SO_REUSEADDR,
        &opt, sizeof (opt));
    errno_assert (rc == 0);

    rc = listen (self->hndl.s, backlog);
    if (sp_slow (rc < 0))
       return -errno;

    /*  Register the socket with the completion port. */
    sp_cp_add_fd (self->cp, self->hndl.s, &self->hndl);

    return 0;
}

int sp_usock_accept (struct sp_usock *self, struct sp_usock *newsock)
{
    sp_cp_pollin (self->cp, &self->hndl);
    return -EINPROGRESS;
}

int sp_usock_send (struct sp_usock *self, const void *buf, size_t *len,
    int flags)
{
    return sp_cp_send (self->cp, &self->hndl, buf, len, flags);
}

int sp_usock_recv (struct sp_usock *self, void *buf, size_t *len,
    int flags)
{
    return sp_cp_recv (self->cp, &self->hndl, buf, len, flags);
}

#endif

