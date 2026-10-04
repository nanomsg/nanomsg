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

#ifndef NN_IPC_TRANSPORT_INCLUDED
#define NN_IPC_TRANSPORT_INCLUDED

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#endif

/*  Identity of the file created by a bound AF_UNIX endpoint. */
struct nn_ipc_file {
#if defined NN_HAVE_WINDOWS
    HANDLE handle;
#else
    dev_t dev;
    ino_t ino;
    int owned;
#endif
};

/*  Returns address size, or a negative errno for an invalid address. */
int nn_ipc_resolve (const char *addr, int domain, struct sockaddr_storage *ss);
void nn_ipc_unlink (const char *addr);
void nn_ipc_file_init (struct nn_ipc_file *self);
void nn_ipc_file_capture (struct nn_ipc_file *self, const char *addr);
void nn_ipc_file_unlink (struct nn_ipc_file *self, const char *addr);
void nn_ipc_file_term (struct nn_ipc_file *self);

#endif
