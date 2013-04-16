/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#ifndef NN_AIO_INCLUDED
#define NN_AIO_INCLUDED

#include "../utils/addr.h"

#include <stddef.h>

/*  These objects are not thread-safe. To make it work correctly, all the calls
    should by synchronised via nn_cp_lock(). All the callbacks are already
    called inside of the same critical section. */

struct nn_cp;
struct nn_timer;
struct nn_usock;
struct nn_event;

#define NN_AIO_MAX_IOVCNT 3

struct nn_iobuf {
    void *iov_base;
    size_t iov_len;
};

struct nn_cp_sink {
    void (*received) (const struct nn_cp_sink **self,
        struct nn_usock *usock);
    void (*sent) (const struct nn_cp_sink **self,
        struct nn_usock *usock);
    void (*connected) (const struct nn_cp_sink **self,
        struct nn_usock *usock);
    void (*accepted) (const struct nn_cp_sink **self,
        struct nn_usock *usock, int s);
    void (*err) (const struct nn_cp_sink **self,
        struct nn_usock *usock, int errnum);
    void (*closed) (const struct nn_cp_sink **self,
        struct nn_usock *usock);
    void (*timeout) (const struct nn_cp_sink **self,
        struct nn_timer *timer);
    void (*event) (const struct nn_cp_sink **self,
        struct nn_event *event);
};

void nn_timer_init (struct nn_timer *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp);
void nn_timer_term (struct nn_timer *self);
void nn_timer_start (struct nn_timer *self, int timeout);
void nn_timer_stop (struct nn_timer *self);

void nn_event_init (struct nn_event *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp);
void nn_event_term (struct nn_event *self);
void nn_event_signal (struct nn_event *self);

int nn_usock_init (struct nn_usock *self, const struct nn_cp_sink **sink,
    int domain, int type, int protocol, int sndbuf, int rcvbuf,
    struct nn_cp *cp);
int nn_usock_init_child (struct nn_usock *self, struct nn_usock *parent,
    int s, const struct nn_cp_sink **sink, int sndbuf, int rcvbuf,
    struct nn_cp *cp);
const struct nn_cp_sink **nn_usock_setsink (struct nn_usock *self,
    const struct nn_cp_sink **sink);
void nn_usock_close (struct nn_usock *self);

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen);
int nn_usock_listen (struct nn_usock *self, int backlog);
void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen);
void nn_usock_accept (struct nn_usock *self);

void nn_usock_send (struct nn_usock *self,
    const struct nn_iobuf *iov, int iovcnt);
void nn_usock_recv (struct nn_usock *self, void *buf, size_t len);

int nn_cp_init (struct nn_cp *self);
void nn_cp_term (struct nn_cp *self);

void nn_cp_lock (struct nn_cp *self);
void nn_cp_unlock (struct nn_cp *self);

#if defined NN_HAVE_WINDOWS

#include "../utils/win.h"
#include "../utils/thread.h"
#include "../utils/mutex.h"

#include "timerset.h"

struct nn_timer {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    struct nn_timerset_hndl hndl;
};

struct nn_event {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    int active;
};

#define NN_USOCK_OP_CONN 1
#define NN_USOCK_OP_RECV 2
#define NN_USOCK_OP_SEND 3
#define NN_USOCK_OP_CONNECT 4
#define NN_USOCK_OP_ACCEPT 5

struct nn_usock_op {
    OVERLAPPED olpd;
    int op;
};

struct nn_usock {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    SOCKET s;
    struct nn_usock_op conn;
    struct nn_usock_op in;
    struct nn_usock_op out;
    SOCKET newsock;
    int domain;
    int type;
    int protocol;
};

struct nn_cp {
    struct nn_mutex sync;
    struct nn_timerset timeout;
    HANDLE hndl;

    /*  The values of these members are never used. They serve just like
        placeholders to provide unique addresses. */
    char stop_event;
    char timer_event;

    struct nn_thread worker;
};

#else

#include "../utils/efd.h"
#include "../utils/queue.h"
#include "../utils/thread.h"
#include "../utils/mutex.h"

#include "poller.h"
#include "timerset.h"

#include <stdint.h>
#include <sys/socket.h>

struct nn_timer {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    struct nn_timerset_hndl hndl;
};

struct nn_event {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    struct nn_queue_item item;
};

#define NN_USOCK_OP_ADD 1
#define NN_USOCK_OP_RM 2
#define NN_USOCK_OP_IN 3
#define NN_USOCK_OP_OUT 4

struct nn_cp_op_hndl {
    struct nn_queue_item item;
    int op;
};

#define NN_USOCK_INOP_NONE 0
#define NN_USOCK_INOP_RECV 1
#define NN_USOCK_INOP_ACCEPT 2

#define NN_USOCK_OUTOP_NONE 0
#define NN_USOCK_OUTOP_SEND 1
#define NN_USOCK_OUTOP_CONNECT 2

#define NN_USOCK_FLAG_REGISTERED 1

#define NN_USOCK_BATCH_SIZE 2048

struct nn_usock {
    const struct nn_cp_sink **sink;
    struct nn_cp *cp;
    int s;
    struct nn_poller_hndl hndl;
    struct nn_cp_op_hndl add_hndl;
    struct nn_cp_op_hndl rm_hndl;
    struct {
        int op;
        uint8_t *buf;
        size_t len;
        struct nn_cp_op_hndl hndl;
        uint8_t *batch;
        size_t batch_len;
        size_t batch_pos;
    } in;
    struct {
        int op;
        struct msghdr hdr;
        struct iovec iov [NN_AIO_MAX_IOVCNT];
        struct nn_cp_op_hndl hndl;
    } out;
    int domain;
    int type;
    int protocol;
    int flags;
};

struct nn_cp {
    struct nn_mutex sync;
    struct nn_timerset timeout;
    struct nn_efd efd;
    struct nn_poller_hndl efd_hndl;
    struct nn_poller poller;
    struct nn_queue opqueue;
    struct nn_mutex events_sync;
    struct nn_queue events;
    int stop;
    struct nn_thread worker;
};

#endif

#endif

