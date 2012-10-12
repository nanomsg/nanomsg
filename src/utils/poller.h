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

#include "config.h"

#include <stdint.h>

/*  Poller object virtualises different polling mechanism that signal whether
    a socket is ready for sending and/or receiving. It's not meant to virtualise
    polling mechanisms that signal I/O operation completaion (IOCP). */

struct sp_poller;
struct sp_poller_hndl;

struct sp_poller_vfptr {
    void (*in) (struct sp_poller *self, struct sp_poller_hndl *hndl);
    void (*out) (struct sp_poller *self, struct sp_poller_hndl *hndl);
    void (*err) (struct sp_poller *self, struct sp_poller_hndl *hndl);
};

void sp_poller_init (struct sp_poller *self, struct sp_poller_vfptr *vfptr);
void sp_poller_term (struct sp_poller *self);
void sp_poller_add_fd (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl);
void sp_poller_rm_fd (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_set_in (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_reset_in (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_set_out (struct sp_poller *self, struct sp_poller_hndl *hndl);
void sp_poller_reset_out (struct sp_poller *self, struct sp_poller_hndl *hndl);

#if defined SP_USE_POLL

#include "thread.h"
#include "signaler.h"

#include <poll.h>

struct sp_poller_hndl {
    int index;
};

struct sp_poller {

    /*  Callback functions. */
    struct sp_poller_vfptr *vfptr;

    /*  Actual number of elements in the pollset. */
    int size;

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

    /*  Used to send signals to the worker thread. */
    struct sp_signaler signaler;

    /*  The worker thread. */
    struct sp_thread worker;
};

#elif defined SP_USE_EPOLL

#include "thread.h"
#include "signaler.h"

#include <sys/epoll.h>

#define SP_POLLER_MAX_EVENTS 32

struct sp_poller_hndl {
    int fd;
    uint32_t events;
};

struct sp_poller {

    /*  Callback functions. */
    struct sp_poller_vfptr *vfptr;

    /*  Current pollset. */
    int ep;

    /*  Number of events being processed at the moment. */
    int nevents;

    /*  Index of the event being processed at the moment. */
    int index;

    /*  Events beign processed at the moment. */
    struct epoll_event events [SP_POLLER_MAX_EVENTS];

    /*  Used to send signals to the worker thread. */
    struct sp_signaler signaler;

    /*  The worker thread. */
    struct sp_thread worker;
};

#else
#error
#endif

#endif

