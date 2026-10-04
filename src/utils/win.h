/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright 2026 Staysail Systems, Inc.

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

#ifndef NN_WIN_INCLUDED
#define NN_WIN_INCLUDED

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <process.h>
#include <ws2tcpip.h>

/*  Use the native AF_UNIX layout, including with older SDKs. */
#if defined NN_HAVE_AFUNIX_H
#include <afunix.h>
#else
struct sockaddr_un {
    ADDRESS_FAMILY sun_family;
    char sun_path [108];
};
#endif

/*  Named pipes retain the address capacity of the original IPC transport.
    This private domain must never be passed to Winsock. */
#define NN_USOCK_WINPIPE -1
struct nn_sockaddr_winpipe {
    short sun_family;
    char sun_path [sizeof (struct sockaddr_storage) -
        sizeof (short)];
};

#define ssize_t int

#endif
