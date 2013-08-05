/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.

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

#include "err.h"

#ifdef NN_HAVE_WINDOWS
#include "win.h"
#endif

#include <stdlib.h>

void nn_err_abort (void)
{
    abort ();
}

int nn_err_errno (void)
{
    return errno;
}

const char *nn_err_strerror (int errnum)
{
    switch (errnum) {
#if defined NN_ENOTSUP_DEFINED
    case ENOTSUP:
        return "Not supported";
#endif
#if defined NN_EPROTONOSUPPORT_DEFINED
    case EPROTONOSUPPORT:
        return "Protocol not supported";
#endif
#if defined NN_ENOBUFS_DEFINED
    case ENOBUFS:
        return "No buffer space available";
#endif
#if defined NN_ENETDOWN_DEFINED
    case ENETDOWN:
        return "Network is down";
#endif
#if defined NN_EADDRINUSE_DEFINED
    case EADDRINUSE:
        return "Address in use";
#endif
#if defined NN_EADDRNOTAVAIL_DEFINED
    case EADDRNOTAVAIL:
        return "Address not available";
#endif
#if defined NN_ECONNREFUSED_DEFINED
    case ECONNREFUSED:
        return "Connection refused";
#endif
#if defined NN_EINPROGRESS_DEFINED
    case EINPROGRESS:
        return "Operation in progress";
#endif
#if defined NN_ENOTSOCK_DEFINED
    case ENOTSOCK:
        return "Not a socket";
#endif
#if defined NN_EAFNOSUPPORT_DEFINED
    case EAFNOSUPPORT:
        return "Address family not supported";
#endif
#if defined NN_EPROTO_DEFINED
    case EPROTO:
        return "Protocol error";
#endif
    case ETERM:
        return "Nanomsg library was terminated";
    case EFSM:
        return "Operation cannot be performed in this state";
    default:
#if defined _MSC_VER
#pragma warning (push)
#pragma warning (disable:4996)
#endif
        return strerror (errnum);
#if defined _MSC_VER
#pragma warning (pop)
#endif
    }
}

#ifdef NN_HAVE_WINDOWS

int nn_err_wsa_to_posix (int wsaerr)
{
    switch (wsaerr) {
    case WSAEINPROGRESS:
        return EAGAIN;
    case WSAEBADF:
        return EBADF;
    case WSAEINVAL:
        return EINVAL;
    case WSAEMFILE:
        return EMFILE;
    case WSAEFAULT:
        return EFAULT;
    case WSAEPROTONOSUPPORT:
        return EPROTONOSUPPORT;
    case WSAENOBUFS:
        return ENOBUFS;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAEAFNOSUPPORT:
        return EAFNOSUPPORT;
    case WSAEACCES:
        return EACCES;
    case WSAENETRESET:
        return ENETRESET;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAEHOSTUNREACH:
        return EHOSTUNREACH;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAEMSGSIZE:
        return EMSGSIZE;
    case WSAETIMEDOUT:
        return ETIMEDOUT;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    default:
        nn_assert (0);
    }
}

void nn_win_error (int err, char *buf, size_t bufsize)
{
    DWORD rc = FormatMessageA (
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD) err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf, (DWORD) bufsize, NULL );
    nn_assert (rc);
}

#endif

