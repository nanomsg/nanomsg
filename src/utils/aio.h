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
#include "addr.h"

#if defined SP_HAVE_WINDOWS
#include "win.h"
#endif

#include <stddef.h>

/*  Implementation of platform-neutral asynchronous I/O subsystem. */

/*  If this flag is set, recv doesn't have to wait for all bytes to be received
    before it completes. */
#define SP_USOCK_PARTIAL 1

/*  These standard operations have negative indices so that positive namespace
    if free for custom events. */
#define SP_USOCK_ACCEPT -1
#define SP_USOCK_CONNECT -2
#define SP_USOCK_SEND -3
#define SP_USOCK_RECV -4

struct sp_cp;
struct sp_usock;

int sp_usock_init (struct sp_usock *self, int domain, int type, int protocol,
    struct sp_cp *cp);
void sp_usock_term (struct sp_usock *self);

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);
int sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);
int sp_usock_listen (struct sp_usock *self, int backlog);
int sp_usock_accept (struct sp_usock *self, struct sp_usock *usock);

int sp_usock_send (struct sp_usock *self, const void *buf, size_t *len,
    int flags);
int sp_usock_recv (struct sp_usock *self, void *buf, size_t *len, int flags);

void sp_cp_init (struct sp_cp *self);
void sp_cp_term (struct sp_cp *self);
void sp_cp_post (struct sp_cp *self, int op, void *arg);

/*  The function is suspectible to spurious ETIMEDOUT wake-ups. */
int sp_cp_wait (struct sp_cp *self, int timeout, int *op,
    struct sp_usock **usock, void **arg);

#if defined SP_HAVE_WINDOWS

struct sp_usock {
    SOCKET s;
    int domain;
    int type;
    int protocol;
    struct sp_cp *cp;
    OVERLAPPED err;
    OVERLAPPED in;
    OVERLAPPED out;
};

struct sp_cp {
    HANDLE hndl;
};

#else

#define SP_USOCK_FLAG_INPROGRESS 1
#define SP_USOCK_FLAG_PARTIAL 2

struct sp_usock {
    int s;
    int domain;
    int type;
    int protocol;
    struct sp_cp *cp;
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

struct sp_cp {
    struct sp_mutex sync;
    struct sp_poller poller;
    struct sp_eventfd eventfd;
    struct sp_poller_hndl evhndl;
    size_t capacity;
    size_t head;
    size_t tail;
    struct sp_cp_item {
        int op;
        void *arg;
    } *items;
};

#endif

#endif

