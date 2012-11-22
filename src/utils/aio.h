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
#include "efd.h"
#include "poller.h"
#include "thread.h"
#include "timeout.h"
#include "queue.h"
#include "addr.h"

#include <stddef.h>

/*  These objects are not thread-safe. To make it work correctly, all the calls
    should by synchronised via sp_cp_lock(). All the callback are already
    called inside of the same critical section. */

struct sp_cp;

/*  Timer object. */

struct sp_timer;

struct sp_timer_vfptr {
    void (*timeout) (struct sp_timer *self);
};

struct sp_timer {
    const struct sp_timer_vfptr *vfptr;
    struct sp_cp *cp;
    struct sp_timeout_hndl hndl;
    int active;
};

void sp_timer_init (struct sp_timer *self, const struct sp_timer_vfptr *vfptr,
    struct sp_cp *cp);
void sp_timer_term (struct sp_timer *self);
void sp_timer_start (struct sp_timer *self, int timeout);
void sp_timer_stop (struct sp_timer *self);

/*  Underlying L4 socket object. */

#define SP_CP_OP_ADD 1
#define SP_CP_OP_RM 2
#define SP_CP_OP_IN 3
#define SP_CP_OP_OUT 4

struct sp_cp_op_hndl {
    struct sp_queue_item item;
    int op;
};

#define SP_USOCK_PARTIAL 1

#define SP_CP_INOP_NONE 0
#define SP_CP_INOP_RECV 1
#define SP_CP_INOP_RECV_PARTIAL 2
#define SP_CP_INOP_ACCEPT 3

#define SP_CP_OUTOP_NONE 0
#define SP_CP_OUTOP_SEND 1
#define SP_CP_OUTOP_SEND_PARTIAL 2
#define SP_CP_OUTOP_CONNECT 3

struct sp_usock;

struct sp_usock_vfptr {
    void (*received) (struct sp_usock *self, size_t len);
    void (*sent) (struct sp_usock *self, size_t len);
    void (*connected) (struct sp_usock *self);
    void (*accepted) (struct sp_usock *self, int s);
    void (*err) (struct sp_usock *self, int errnum);
};

struct sp_usock {
    const struct sp_usock_vfptr *vfptr;
    int s;
    struct sp_cp *cp;
    struct sp_poller_hndl hndl;
    struct sp_cp_op_hndl add_hndl;
    struct sp_cp_op_hndl rm_hndl;
    struct {
        int op;
        void *buf;
        size_t buflen;
        size_t len;
        struct sp_cp_op_hndl hndl;
    } in;
    struct {
        int op;
        const void *buf;
        size_t buflen;
        size_t len;
        struct sp_cp_op_hndl hndl;
    } out;
    int domain;
    int type;
    int protocol;
};

int sp_usock_init (struct sp_usock *self, const struct sp_usock_vfptr *vfptr,
    int domain, int type, int protocol, struct sp_cp *cp);
void sp_usock_term (struct sp_usock *self);

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);
int sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);
int sp_usock_listen (struct sp_usock *self, int backlog);
int sp_usock_accept (struct sp_usock *self);

int sp_usock_send (struct sp_usock *self, const void *buf, size_t *len,
    int flags);
int sp_usock_recv (struct sp_usock *self, void *buf, size_t *len, int flags);

/*  The completion port. */

struct sp_event_hndl {
    struct sp_queue_item item;
    int event;
};

struct sp_cp;

struct sp_cp_vfptr {
    void (*event) (struct sp_cp *self, int event, struct sp_event_hndl *hndl);
};

struct sp_cp {
    const struct sp_cp_vfptr *vfptr;
    struct sp_mutex sync;
    struct sp_timeout timeout;
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

void sp_cp_post (struct sp_cp *self, int event, struct sp_event_hndl *hndl);

#endif

