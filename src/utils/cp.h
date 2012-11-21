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

#ifndef SP_CP_INCLUDED
#define SP_CP_INCLUDED

#include "mutex.h"
#include "efd.h"
#include "poller.h"
#include "thread.h"
#include "timer.h"
#include "queue.h"

#include <stddef.h>

/*  This object is not thread-safe. To make it work correctly, all the calls
    should by synchronised via sp_cp_lock(). */

#define SP_CP_PARTIAL 1

struct sp_cp_timer_hndl;

/*  Timer handle definition. */

struct sp_cp_timer_vfptr {
    void (*timeout) (struct sp_cp_timer_hndl *self);
};

struct sp_cp_timer_hndl {
    const struct sp_cp_timer_vfptr *vfptr;
    struct sp_timer_hndl hndl;
};

/*  Event handle definition. */

struct sp_event_hndl {
    struct sp_queue_item item;
    int event;
};

/*  Operation handle. Used internally by sp_cp. */

#define SP_CP_OP_ADD 1
#define SP_CP_OP_RM 2
#define SP_CP_OP_IN 3
#define SP_CP_OP_OUT 4

struct sp_op_hndl {
    struct sp_queue_item item;
    int op;
};

/*  I/O handle definition. */

#define SP_CP_INOP_NONE 0
#define SP_CP_INOP_RECV 1
#define SP_CP_INOP_RECV_PARTIAL 2
#define SP_CP_INOP_POLLIN 3

#define SP_CP_OUTOP_NONE 0
#define SP_CP_OUTOP_SEND 1
#define SP_CP_OUTOP_SEND_PARTIAL 2
#define SP_CP_OUTOP_POLLOUT 3

struct sp_io_hndl {
    int s;
    struct sp_poller_hndl hndl;
    struct sp_op_hndl add_hndl;
    struct sp_op_hndl rm_hndl;
    struct {
        int op;
        void *buf;
        size_t buflen;
        size_t len;
        struct sp_op_hndl hndl;
    } in;
    struct {
        int op;
        const void *buf;
        size_t buflen;
        size_t len;
        struct sp_op_hndl hndl;
    } out;
};

/*  The completion port. */

struct sp_cp;

struct sp_cp_vfptr {
    void (*in) (struct sp_cp *self, struct sp_io_hndl *hndl);
    void (*out) (struct sp_cp *self, struct sp_io_hndl *hndl);
    void (*err) (struct sp_cp *self, struct sp_io_hndl *hndl);
    void (*event) (struct sp_cp *self, int event, struct sp_event_hndl *hndl);
};

struct sp_cp {
    const struct sp_cp_vfptr *vfptr;
    struct sp_mutex sync;
    struct sp_timer timer;
    struct sp_efd efd;
    struct sp_poller_hndl efd_hndl;
    struct sp_poller poller;
    struct sp_queue opqueue;
    struct sp_mutex events_sync;
    struct sp_queue events;
    int stop;
    struct sp_thread worker;
};

void sp_cp_init (struct sp_cp *self, const struct sp_cp_vfptr *vfptr);
void sp_cp_term (struct sp_cp *self);

void sp_cp_lock (struct sp_cp *self);
void sp_cp_unlock (struct sp_cp *self);

void sp_cp_add_timer (struct sp_cp *self, int timeout,
    const struct sp_cp_timer_vfptr *vfptr, struct sp_cp_timer_hndl *hndl);
void sp_cp_rm_timer (struct sp_cp *self, struct sp_cp_timer_hndl *hndl);

void sp_cp_post (struct sp_cp *self, int event, struct sp_event_hndl *hndl);

void sp_cp_add_fd (struct sp_cp *self, int s, struct sp_io_hndl *hndl);
void sp_cp_rm_fd (struct sp_cp *self, struct sp_io_hndl *hndl);

void sp_cp_pollin (struct sp_cp *self, struct sp_io_hndl *hndl);
void sp_cp_pollout (struct sp_cp *self, struct sp_io_hndl *hndl);

int sp_cp_send (struct sp_cp *self, struct sp_io_hndl *hndl, const void *buf,
    size_t *len, int flags);
int sp_cp_recv (struct sp_cp *self, struct sp_io_hndl *hndl, void *buf,
    size_t *len, int flags);

#endif

