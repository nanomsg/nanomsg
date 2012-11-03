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
#include "fast.h"
#include "err.h"

#if defined SP_HAVE_ACCEPT4
#define _GNU_SOURCE
#endif

#if !defined SP_HAVE_WINDOWS
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

/*  Private functions. */
void sp_usock_tune (struct sp_usock *self);

int sp_usock_init (struct sp_usock *self, int domain, int type, int protocol)
{
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    int rc;
#endif

    /*  If the operating system allows to directly open the socket with CLOEXEC
        flag, do so. That way there are no race conditions. */
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif

    /*  Open the underlying socket. */
    self->s = socket (domain, type, protocol);
#if defined SP_HAVE_WINDOWS
    if (self->s == INVALID_SOCKET)
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (self->s < 0)
       return -errno;
#endif
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;
#if !defined SP_HAVE_WINDOWS
    self->aio = NULL;
#endif

    /*  Setting FD_CLOEXEC option immediately after socket creation is the
        second best option. There is a race condition (if process is forked
        between socket creation and setting the option) but the problem is
        pretty unlikely to happen. */
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    rc = fcntl (self->s, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    sp_usock_tune (self);

    return 0;
}

void sp_usock_term (struct sp_usock *self)
{
    int rc;

#if defined SP_HAVE_WINDOWS
    rc = closesocket (self->s);
    wsa_assert (rc != SOCKET_ERROR);
#else
    rc = close (self->s);
    errno_assert (rc == 0);
#endif
}

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
{
    int rc;
    int opt;

    /*  To allow for rapid restart of SP services, allow new bind to succeed
        immediately after previous instance of the process failed, skipping the
        grace period. */
#if !defined SP_HAVE_WINDOWS
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
    errno_assert (rc == 0);
#endif

    /*  On Windows, the bound port can be hijacked if SO_EXCLUSIVEADDRUSE
        is not set. */
#if defined SP_HAVE_WINDOWS
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*) &opt, sizeof (opt));
    wsa_assert (rc != SOCKET_ERROR);
#endif

    rc = bind (self->s, addr, addrlen);
#if defined SP_HAVE_WINDOWS
    if (sp_slow (rc == SOCKET_ERROR))
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (sp_slow (rc < 0))
       return -errno;
#endif

    return 0;
}

int sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen)
{
    int rc;

    rc = connect (self->s, addr, addrlen);
#if defined SP_HAVE_WINDOWS
    if (sp_slow (rc == SOCKET_ERROR))
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (sp_slow (rc < 0))
       return -errno;
#endif

    return 0;
}

int sp_usock_listen (struct sp_usock *self, int backlog)
{
    int rc;

    rc = listen (self->s, backlog);
#if defined SP_HAVE_WINDOWS
    if (sp_slow (rc == SOCKET_ERROR))
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (sp_slow (rc < 0))
       return -errno;
#endif

    return 0;
}

int sp_usock_accept (struct sp_usock *self, struct sp_usock *accepted)
{
    int rc;

#if defined SP_HAVE_ACCEPT4
    rc = accept4 (self->s, NULL, NULL, SOCK_CLOEXEC);
#else
    rc = accept (self->s, NULL, NULL);
#endif
#if defined SP_HAVE_WINDOWS
    if (sp_slow (rc == INVALID_SOCKET))
       return -sp_err_wsa_to_posix (WSAGetLastError ());
#else
    if (sp_slow (rc < 0))
       return -errno;
#endif

    /*  Create the new usock object. */
    accepted->s = rc;
    accepted->domain = self->domain;
    accepted->type = self->type;
    accepted->protocol = self->protocol;
#if !defined SP_HAVE_WINDOWS
    accepted->aio = NULL;
#endif

    /*  If CLOEXEC is not yet set, apply it to the new socket. */
#if !defined SP_HAVE_ACCEPT && defined FD_CLOEXEC
    rc = fcntl (accepted->s, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    sp_usock_tune (accepted);

    return 0;
}

void sp_usock_tune (struct sp_usock *self)
{
    int rc;
    int opt;
#ifdef _WIN32
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
    rc = setsockopt (self->s, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof (opt));
    errno_assert (rc == 0);
#endif

    /*  Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the asynchronous mode. */
#if defined SP_HAVE_WINDOWS
    flags = 1;
    rc = ioctlsocket (self->s, FIONBIO, &flags);
    wsa_assert (rc != SOCKET_ERROR);
#else
	flags = fcntl (self->s, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	rc = fcntl (self->s, F_SETFL, flags | O_NONBLOCK);
    errno_assert (rc != -1);
#endif

    /*  On TCP sockets switch off the Nagle's algorithm to get
        the best possible latency. */
    if ((self->domain == AF_INET || self->domain == AF_INET6) &&
          self->type == SOCK_STREAM) {
        opt = 1;
        rc = setsockopt (self->s, IPPROTO_TCP, TCP_NODELAY,
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
    rc = setsockopt (self->s, IPPROTO_TCP, TCP_NODELACK, &opt, sizeof (opt));
    errno_assert (rc == 0);
#endif

    /*  On some operating systems IPv4 mapping for IPv6 sockets is disabled
        by default. In such case, switch it on. */
#if defined IPV6_V6ONLY
    if (self->domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->s, IPPROTO_IPV6, IPV6_V6ONLY,
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
    brc = SetHandleInformation ((HANDLE) self->s, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);
#endif
}

