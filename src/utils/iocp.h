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

#ifndef SP_IOCP_INCLUDED
#define SP_IOCP_INCLUDED

#include <stddef.h>

#if defined SP_HAVE_WINDOWS
#include "win.h"
#endif

/*  Platform-independent implementation of I/O completion ports. */

struct sp_usock;

struct sp_iocp {
#if defined SP_HAVE_WINDOWS
    HANDLE cp;
#else
#endif
};

struct sp_iocp_task {
#if defined SP_HAVE_WINDOWS
    OVERLAPPED io;
#else
    void *buf;
    size_t len;
    size_t nbytes;
#endif
};

void sp_iocp_init (struct sp_iocp *self);
void sp_iocp_term (struct sp_iocp *self);

void sp_iocp_register (struct sp_iocp *self, struct sp_usock *usock);

int sp_iocp_wait (struct sp_iocp *self, int timeout,
    struct sp_iocp_task **task, size_t *len);

#endif

