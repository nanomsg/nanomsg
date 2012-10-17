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

#include "err.h"

#ifdef SP_HAVE_WINDOWS
#include "win.h"
#endif

#include <stdlib.h>

void sp_err_abort (void)
{
    abort ();
}

int sp_err_errno (void)
{
    return errno;
}

const char *sp_err_strerror (int errnum)
{
    switch (errnum) {
#if defined SP_ENOTSUP_DEFINED
    case ENOTSUP:
        return "Not supported";
#endif
#if defined SP_EPROTONOSUPPORT_DEFINED
    case EPROTONOSUPPORT:
        return "Protocol not supported";
#endif
#if defined SP_ENOBUFS_DEFINED
    case ENOBUFS:
        return "No buffer space available";
#endif
#if defined SP_ENETDOWN_DEFINED
    case ENETDOWN:
        return "Network is down";
#endif
#if defined SP_EADDRINUSE_DEFINED
    case EADDRINUSE:
        return "Address in use";
#endif
#if defined SP_EADDRNOTAVAIL_DEFINED
    case EADDRNOTAVAIL:
        return "Address not available";
#endif
#if defined SP_ECONNREFUSED_DEFINED
    case ECONNREFUSED:
        return "Connection refused";
#endif
#if defined SP_EINPROGRESS_DEFINED
    case EINPROGRESS:
        return "Operation in progress";
#endif
#if defined SP_ENOTSOCK_DEFINED
    case ENOTSOCK:
        return "Not a socket";
#endif
#if defined SP_EAFNOTSUPPORT_DEFINED
    case EAFNOSUPPORT:
        return "Address family not supported";
#endif
    case ETERM:
        return "SP subsystem was terminated";
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

#ifdef SP_HAVE_WINDOWS

void sp_win_error (int err, char *buf, size_t bufsize)
{
    DWORD rc = FormatMessageA (
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD) err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buf, (DWORD) bufsize, NULL );
    sp_assert (rc);
}

#endif

