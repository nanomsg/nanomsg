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

#ifndef SP_AIO_INCLUDED
#define SP_AIO_INCLUDED

#include "mutex.h"
#include "poller.h"
#include "eventfd.h"

#if defined SP_HAVE_WINDOWS
#include "win.h"
#endif

#include <stddef.h>

/*  Implementation of platform-neutral asynchronous I/O subsystem. */

struct sp_aio;
struct sp_aio_hndl;
struct sp_usock;

void sp_aio_init (struct sp_aio *self);
void sp_aio_term (struct sp_aio *self);
void sp_aio_post (struct sp_aio *self, int op, void *arg);

/*  The function is suspectible to spurious ETIMEDOUT wake-ups. */
int sp_aio_wait (struct sp_aio *self, int timeout, int *op, void **arg);

/*  Associates the socket with the completion point. Association is broken
    when the socket is destroyed. */
void sp_aio_register_usock (struct sp_aio *self, struct sp_usock *usock);

#if defined SP_HAVE_WINDOWS

struct sp_aio_hndl {
    OVERLAPPED olpd;
};

struct sp_aio {
    HANDLE hndl;
};

#else

#define SP_CP_IN_PROGRESS 1
#define SP_CP_PARTIAL 2

#define SP_CP_IN 1
#define SP_CP_OUT 2
#define SP_CP_ERR 3

struct sp_aio_hndl {
    int fd;
    struct sp_poller_hndl hndl;
    struct {
        int flags;
        void *buf;
        size_t len;
        size_t olen;
    } in;
    struct {
        int flags;
        const void *buf;
        size_t len;
        size_t olen;
    } out;
};

struct sp_aio {
    struct sp_mutex sync;
    struct sp_poller poller;
    struct sp_eventfd eventfd;
    struct sp_poller_hndl evhndl;
    size_t capacity;
    size_t head;
    size_t tail;
    struct sp_aio_item {
        int op;
        void *arg;
    } *items;
};

#endif

#endif

