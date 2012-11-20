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
#include "eventfd.h"
#include "poller.h"
#include "thread.h"
#include "timer.h"
#include "list.h"

#include <stddef.h>

/*  This object is not thread-safe. To make it work correctly, all the calls
    should by synchronised via sp_aio_lock(). */

#define SP_AIO_IN 1
#define SP_AIO_OUT 2
#define SP_AIO_ERR 3

struct sp_aio;

struct sp_event_hndl {
    struct sp_list_item list;
    int event;
};

#define SP_AIO_INOP_RECV 1
#define SP_AIO_INOP_RECV_PARTIAL 2
#define SP_AIO_INOP_POLLIN 3

#define SP_AIO_OUTOP_SEND 1
#define SP_AIO_OUTOP_SEND_PARTIAL 2
#define SP_AIO_OUTOP_POLLOUT 3

struct sp_io_hndl {
    int s;
    struct sp_poller_hndl hndl;
    struct {
        int op;
        void *buf;
        size_t buflen;
        size_t len;
    } in;
    struct {
        int op;
        const void *buf;
        size_t buflen;
        size_t len;
    } out;
};

struct sp_aio_vfptr {
    void (*io) (struct sp_aio *self, int event, struct sp_io_hndl *hndl);
    void (*event) (struct sp_aio *self, int event, struct sp_event_hndl *hndl);
    void (*timeout) (struct sp_aio *self, struct sp_timer_hndl *hndl);
};

struct sp_aio {
    const struct sp_aio_vfptr *vfptr;
    struct sp_mutex sync;
    struct sp_timer timer;
    struct sp_eventfd efd;
    struct sp_poller_hndl efd_hndl;
    struct sp_poller poller;
    struct sp_mutex events_sync;
    struct sp_list events;
    int stop;
    struct sp_thread worker;
};

void sp_aio_init (struct sp_aio *self, const struct sp_aio_vfptr *vfptr);
void sp_aio_term (struct sp_aio *self);

void sp_aio_lock (struct sp_aio *self);
void sp_aio_unlock (struct sp_aio *self);

void sp_aio_add_timer (struct sp_aio *self, int timeout,
    struct sp_timer_hndl *hndl);
void sp_aio_rm_timer (struct sp_aio *self, struct sp_timer_hndl *hndl);

void sp_aio_post (struct sp_aio *self, int event, struct sp_event_hndl *hndl);

#endif

