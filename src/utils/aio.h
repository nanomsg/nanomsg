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

#if !defined SP_HAVE_WINDOWS

#include "poller.h"

#include <stddef.h>

#define SP_AIO_IN_PROGRESS 1
#define SP_AIO_PARTIAL 2

#define SP_AIO_IN 1
#define SP_AIO_OUT 2
#define SP_AIO_ERR 3

struct sp_aio_hndl {
    int fd;
    struct sp_poller_hndl hndl;
    struct {
        int flags;
        void *buf;
        size_t len;
    } in;
    struct {
        int flags;
        const void *buf;
        size_t len;
    } out;
};

struct sp_aio {
    struct sp_poller poller;
};

void sp_aio_init (struct sp_aio *self);
void sp_aio_term (struct sp_aio *self);

void sp_aio_register (struct sp_aio *self, int fd, struct sp_aio_hndl *hndl);
void sp_aio_unregister (struct sp_aio *self, struct sp_aio_hndl *hndl);

int sp_aio_send (struct sp_aio *self, struct sp_aio_hndl *hndl,
    const void *buf, size_t len, int flags);
int sp_aio_recv (struct sp_aio *self, struct sp_aio_hndl *hndl,
    void *buf, size_t len, int flags);

int sp_aio_wait (struct sp_aio *self, int timeout, int *event,
    struct sp_aio_hndl **hndl);

#endif

#endif

