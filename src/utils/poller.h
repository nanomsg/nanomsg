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

#ifndef SP_POLLER_INCLUDED
#define SP_POLLER_INCLUDED

#if !defined SP_HAVE_WINDOWS

#define SP_POLLER_IN 1
#define SP_POLLER_OUT 2
#define SP_POLLER_ERR 3

struct sp_poller;
struct sp_poller_hndl;

void sp_poller_init (struct sp_poller *self);
void sp_poller_term (struct sp_poller *self);
void sp_poller_add (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl);
void sp_poller_rm (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_set_in (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_reset_in (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_set_out (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_reset_out (struct sp_poller *self, struct sp_poller_hndl *hndl);
int sp_poller_wait (struct sp_poller *self, int timeout);
int sp_poller_event (struct sp_poller *self, int *event,
    struct sp_poller_hndl **hndl);

#if defined SP_USE_POLL

#include <poll.h>

struct sp_poller_hndl {
    int index;
};

struct sp_poller {

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
    struct sp_hndls_item {
        struct sp_poller_hndl *hndl;
        int prev;
        int next;
    } *hndls;

    /*  List of removed pollitems, linked by indices. -1 means empty list. */
    int removed;
};

#endif

#if defined SP_USE_EPOLL

#include <sys/epoll.h>

#define SP_POLLER_MAX_EVENTS 32

struct sp_poller_hndl {
    int fd;
    uint32_t events;
};

struct sp_poller {

    /*  Current pollset. */
    int ep;

    /*  Number of events being processed at the moment. */
    int nevents;

    /*  Index of the event being processed at the moment. */
    int index;

    /*  Events beign processed at the moment. */
    struct epoll_event events [SP_POLLER_MAX_EVENTS];
};

#endif

#endif

#endif

