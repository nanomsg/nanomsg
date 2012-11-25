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
#include "err.h"

#if defined SP_HAVE_WINDOWS
#include "win.h"
#else
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

/*  This file contains functions common to all OS platforms.
    Then in includes either aio_posix.inc or aio_win.inc depending
    on the platform to get platform-specific functions. */

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
#if defined SP_HAVE_WINDOWS
        ;
#else
        sp_efd_signal (&self->cp->efd);
#endif
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
#if defined SP_HAVE_WINDOWS
        ;
#else
        sp_efd_signal (&self->cp->efd);
#endif
}

int sp_usock_init (struct sp_usock *self, const struct sp_sink **sink,
    int domain, int type, int protocol, struct sp_cp *cp)
{
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    int rc;
#endif
#if defined SP_HAVE_WINDOWS
    HANDLE wcp;
#endif

    self->sink = sink;
    self->cp = cp;
#if !defined SP_HAVE_WINDOWS
    self->in.op = SP_USOCK_INOP_NONE;
    self->out.op = SP_USOCK_OUTOP_NONE;
    self->add_hndl.op = SP_USOCK_OP_ADD;
    self->rm_hndl.op = SP_USOCK_OP_RM;
    self->in.hndl.op = SP_USOCK_OP_IN;
    self->out.hndl.op = SP_USOCK_OP_OUT;
#endif
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;

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

    /*  Setting FD_CLOEXEC option immediately after socket creation is the
        second best option. There is a race condition (if process is forked
        between socket creation and setting the option) but the problem is
        pretty unlikely to happen. */
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    rc = fcntl (self->s, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    sp_usock_tune (self);

#if defined SP_HAVE_WINDOWS
    wcp = CreateIoCompletionPort ((HANDLE) self->s, cp->hndl,
        (ULONG_PTR) NULL, 0);
    sp_assert (wcp);
#endif

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
#if !defined SP_HAVE_WINDOWS
    self->in.op = SP_USOCK_INOP_NONE;
    self->out.op = SP_USOCK_OUTOP_NONE;
    self->add_hndl.op = SP_USOCK_OP_ADD;
    self->rm_hndl.op = SP_USOCK_OP_RM;
    self->in.hndl.op = SP_USOCK_OP_IN;
    self->out.hndl.op = SP_USOCK_OP_OUT;
#endif
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
    rc = setsockopt (self->s, SOL_SOCKET, SO_NOSIGPIPE,
        &opt, sizeof (opt));
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

#if defined SP_HAVE_WINDOWS
#include "aio_win.inc"
#else
#include "aio_posix.inc"
#endif

