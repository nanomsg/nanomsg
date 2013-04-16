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

#ifndef NN_POLLER_INCLUDED
#define NN_POLLER_INCLUDED

#if !defined NN_HAVE_WINDOWS

#define NN_POLLER_IN 1
#define NN_POLLER_OUT 2
#define NN_POLLER_ERR 3

struct nn_poller;
struct nn_poller_hndl;

int nn_poller_init (struct nn_poller *self);
void nn_poller_term (struct nn_poller *self);
void nn_poller_add (struct nn_poller *self, int fd,
    struct nn_poller_hndl *hndl);
void nn_poller_rm (struct nn_poller *self, struct nn_poller_hndl *hndl);
void nn_poller_set_in (struct nn_poller *self, struct nn_poller_hndl *hndl);
void nn_poller_reset_in (struct nn_poller *self, struct nn_poller_hndl *hndl);
void nn_poller_set_out (struct nn_poller *self, struct nn_poller_hndl *hndl);
void nn_poller_reset_out (struct nn_poller *self, struct nn_poller_hndl *hndl);
int nn_poller_wait (struct nn_poller *self, int timeout);
int nn_poller_event (struct nn_poller *self, int *event,
    struct nn_poller_hndl **hndl);

#if defined NN_USE_POLL

#include <poll.h>

#define NN_POLLER_HAVE_ASYNC_ADD 0

struct nn_poller_hndl {
    int index;
};

struct nn_poller {

    /*  Actual number of elements in the pollset. */
    int size;

    /*  Index of the event being processed at the moment. */
    int index;

    /*  Number of allocated elements in the pollset. */
    int capacity;

    /*  The pollset. */
    struct pollfd *pollset;

    /*  List of handles associated with elements in the pollset. Either points
        to the handle associated with the file descriptor (hndl) or is part
        of the list of removed pollitems (removed). */
    struct nn_hndls_item {
        struct nn_poller_hndl *hndl;
        int prev;
        int next;
    } *hndls;

    /*  List of removed pollitems, linked by indices. -1 means empty list. */
    int removed;
};

#endif

#if defined NN_USE_EPOLL

#include <sys/epoll.h>

#define NN_POLLER_HAVE_ASYNC_ADD 1

#define NN_POLLER_MAX_EVENTS 32

struct nn_poller_hndl {
    int fd;
    uint32_t events;
};

struct nn_poller {

    /*  Current pollset. */
    int ep;

    /*  Number of events being processed at the moment. */
    int nevents;

    /*  Index of the event being processed at the moment. */
    int index;

    /*  Events being processed at the moment. */
    struct epoll_event events [NN_POLLER_MAX_EVENTS];
};

#endif

#if defined NN_USE_KQUEUE

#include <sys/time.h>
#include <sys/types.h>
#include <sys/event.h>

#define NN_POLLER_MAX_EVENTS 32

#define NN_POLLER_EVENT_IN 1
#define NN_POLLER_EVENT_OUT 2

struct nn_poller_hndl {
    int fd;
    int events;
};

struct nn_poller {

    /*  Current pollset. */
    int kq;

    /*  Number of events being processed at the moment. */
    int nevents;

    /*  Index of the event being processed at the moment. */
    int index;

    /*  Cached events. */
    struct kevent events [NN_POLLER_MAX_EVENTS];
};

#endif

#endif

#endif

