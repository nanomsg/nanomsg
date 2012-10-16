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

#include "poller.h"

#if defined SP_USE_POLL

#include "thread.h"
#include "signaler.h"
#include "alloc.h"
#include "err.h"

#include <poll.h>

#define SP_POLLER_GRANULARITY 16

struct sp_poller_ctx {

    /*  Actual number of elements in the pollset. */
    int size;

    /*  Number of allocated elements in the pollset. */
    int capacity;

    /*  The pollset. */
    struct pollfd *pollset;

    /*  List of poller objects associated with elements in the pollset.
        It either points to the poller associated with the file descriptor
        or is part of the list of removed pollitems (removed). */
    struct sp_poller_item {
        struct sp_poller *poller;
        int prev;
        int next;
    } *pollers;

    /*  List of removed pollitems, linked by indices. -1 means empty list. */
    int removed;

    /*  Used to send signals to the worker thread. */
    struct sp_signaler signaler;

    /*  The worker thread. */
    struct sp_thread worker;
};

/*  Singleton instance of sp_poller_ctx. */
static struct sp_poller_ctx self;

/*  Forward declarations. */
static void sp_poller_routine (void *arg);

void sp_poller_ctx_init (void)
{
    self.size = 1;
    self.capacity = SP_POLLER_GRANULARITY;
    self.pollset =
        sp_alloc (sizeof (struct pollfd*) * SP_POLLER_GRANULARITY);
    alloc_assert (self.pollset);
    self.pollset [0].fd = sp_signaler_fd (&self.signaler);
    self.pollset [0].events = POLLIN;
    self.pollers =
        sp_alloc (sizeof (struct sp_poller_item) * SP_POLLER_GRANULARITY);
    alloc_assert (self.pollers);
    self.pollers [0].poller = NULL;
    self.removed = -1;

    /*  Start the worker thread. */
    sp_signaler_init (&self.signaler);
    sp_thread_init (&self.worker, sp_poller_routine, NULL);
}

void sp_poller_ctx_term (void)
{
    /*  Ask the worker thread to terminate. */
    sp_signaler_post (&self.signaler);

    /*  Wait while worker thread terminates. */
    sp_thread_term (&self.worker);
    sp_signaler_term (&self.signaler);

    /*  Deallocate the resources. */
    sp_free (self.pollset);
    sp_free (self.pollers);
}

void sp_poller_init (struct sp_poller *poller,
    struct sp_poller_vfptr *vfptr, int fd)
{
    int rc;

    poller->vfptr = vfptr;

    /*  Add the file descriptor to the pollset. */
    if (sp_slow (self.size >= self.capacity)) {
        self.capacity += SP_POLLER_GRANULARITY;
        self.pollset = sp_realloc (self.pollset,
            sizeof (struct pollfd*) * self.capacity);
        alloc_assert (self.pollset);
        self.pollers = sp_realloc (self.pollers,
            sizeof (struct sp_poller_item) * self.capacity);
        alloc_assert (self.pollers);
    }
    self.pollset [self.size].fd = fd;
    self.pollset [self.size].events = 0;
    self.pollset [self.size].revents = 0;
    poller->index = self.size;
    self.pollers [self.size].poller = poller;
    ++self.size;
}

void sp_poller_term (struct sp_poller *poller)
{
    /*  Add the fd to the list of removed file descriptors. */
    self.pollset [poller->index].revents = 0;
    if (self.removed != -1)
        self.pollers [self.removed].prev = poller->index;
    self.pollers [poller->index].poller = NULL;
    self.pollers [poller->index].prev = -1;
    self.pollers [poller->index].next = self.removed;
    self.removed = poller->index;
}

void sp_poller_set_in (struct sp_poller *poller)
{
    self.pollset [poller->index].events |= POLLIN;
}

void sp_poller_reset_in (struct sp_poller *poller)
{
    self.pollset [poller->index].events &= ~POLLIN;
    self.pollset [poller->index].revents &= ~POLLIN;
}

void sp_poller_set_out (struct sp_poller *poller)
{
    self.pollset [poller->index].events |= POLLOUT;
}

void sp_poller_reset_out (struct sp_poller *poller)
{
    self.pollset [poller->index].events &= ~POLLOUT;
    self.pollset [poller->index].revents &= ~POLLOUT;
}

static void sp_poller_routine (void *arg)
{
    int rc;
    int timeout;
    int i;
    struct sp_poller *poller;

    while (1) {

        /*  Wait for both network events and async operation requests. */
        rc = poll (self.pollset, self.size, -1);
        if (sp_slow (rc < 0 && errno == EINTR))
            continue;
        if (rc == 0)
            continue;
        errno_assert (rc >= 0);

        /*  If required, terminate the worker thread. */
        if (self.pollset [0].revents & POLLIN)
            break;
        
        for (i = 1; i != self.size; ++i) {

            /*  The descriptor may already have been removed. */
            if (!self.pollers [i].poller)
                continue;

            /*  Network events. Invoke appropriate callbacks. */
            poller = self.pollers [i].poller;
            sp_assert (poller->index == i);
            if (self.pollset [i].revents & POLLOUT)
                poller->vfptr->out (poller);
            if (!self.pollers [i].poller)
                continue;
            if (self.pollset [i].revents & POLLIN)
                poller->vfptr->in (poller);
            if (!self.pollers [i].poller)
                continue;
            if (self.pollset [i].revents & ~(POLLIN | POLLOUT))
                poller->vfptr->err (poller);
        }

        /*  Physically delete all the previosuly removed file descriptors from
            the pollset. */
        while (self.removed != -1) {
            i = self.removed;
            self.removed = self.pollers [i].next;
            self.pollset [i] = self.pollset [self.size - 1];
            self.pollers [i] = self.pollers [self.size - 1];
            self.pollers [i].poller->index = i;

            if (!self.pollers [i].poller) {
                if (self.pollers [i].prev != -1)
                   self.pollers [self.pollers [i].prev].next = i;
                if (self.pollers [i].next != -1)
                   self.pollers [self.pollers [i].next].prev = i;
                if (self.removed == self.size - 1)
                    self.removed = i;
            }
            --self.size;
        }
    }
}

#elif defined SP_USE_EPOLL

#include "err.h"
#include "thread.h"
#include "signaler.h"

#include <sys/epoll.h>
#include <string.h>

#define SP_POLLER_MAX_EVENTS 32

struct sp_poller_ctx {

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

/*  Singleton instance of sp_poller_ctx. */
static struct sp_poller_ctx self;

/*  Forward declarations. */
static void sp_poller_routine (void *arg);

void sp_poller_ctx_init (void)
{
    int rc;
    struct epoll_event ev;

    sp_signaler_init (&self.signaler);
    self.ep = epoll_create1 (EPOLL_CLOEXEC);
    errno_assert (self.ep != -1);
    memset (&ev, 0, sizeof (ev));
    ev.events = EPOLLIN;
    rc = epoll_ctl (self.ep, EPOLL_CTL_ADD,
        sp_signaler_fd (&self.signaler), &ev);
    errno_assert (rc == 0);

    /*  Start the worker thread. */
    sp_thread_init (&self.worker, sp_poller_routine, NULL);
}

void sp_poller_ctx_term (void)
{
    int rc;

    /*  Ask the worker thread to terminate. */
    sp_signaler_post (&self.signaler);

    /*  Wait while worker thread terminates. */
    sp_thread_term (&self.worker);
    sp_signaler_term (&self.signaler);

    /*  Deallocate the resources. */
    rc = close (self.ep);
    errno_assert (rc == 0);
}

void sp_poller_init (struct sp_poller *poller,
    struct sp_poller_vfptr *vfptr, int fd)
{
    int rc;
    struct epoll_event ev;

    poller->vfptr = vfptr;
    poller->fd = fd;
    poller->events = 0;

    /*  Add the file descriptor to the pollset. */
    memset (&ev, 0, sizeof (ev));
    ev.events = 0;
    ev.data.ptr = (void*) poller;
    rc = epoll_ctl (self.ep, EPOLL_CTL_ADD, fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_term (struct sp_poller *poller)
{
    int rc;
    int i;

    /*  Remove the file descriptor from the pollset. */
    rc = epoll_ctl (self.ep, EPOLL_CTL_DEL, poller->fd, NULL);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent events on this file descriptor. */
    for (i = self.index; i != self.nevents; ++i)
        if (self.events [i].data.ptr == poller)
            self.events [i].events = 0;
}

void sp_poller_set_in (struct sp_poller *poller)
{
    int rc;
    struct epoll_event ev;

    /*  If already polling for IN, do nothing. */
    if (sp_slow (poller->events & EPOLLIN))
        return;

    /*  Start polling for IN. */
    poller->events |= EPOLLIN;
    memset (&ev, 0, sizeof (ev));
    ev.events = poller->events;
    ev.data.ptr = (void*) poller;
    rc = epoll_ctl (self.ep, EPOLL_CTL_MOD, poller->fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_reset_in (struct sp_poller *poller)
{
    int rc;
    int i;
    struct epoll_event ev;

    /*  If not polling for IN, do nothing. */
    if (sp_slow (!(poller->events & EPOLLIN)))
        return;

    /*  Stop polling for IN. */
    poller->events & ~EPOLLIN;
    memset (&ev, 0, sizeof (ev));
    ev.events = poller->events;
    ev.data.ptr = (void*) poller;
    rc = epoll_ctl (self.ep, EPOLL_CTL_MOD, poller->fd, &ev);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent IN events on this file descriptor. */
    for (i = self.index; i != self.nevents; ++i)
        if (self.events [i].data.ptr == poller)
            self.events [i].events &= ~EPOLLIN;
}

void sp_poller_set_out (struct sp_poller *poller)
{
    int rc;
    struct epoll_event ev;

    /*  If already polling for OUT, do nothing. */
    if (sp_slow (poller->events & EPOLLOUT))
        return;

    /*  Start polling for OUT. */
    poller->events |= EPOLLOUT;
    memset (&ev, 0, sizeof (ev));
    ev.events = poller->events;
    ev.data.ptr = (void*) poller;
    rc = epoll_ctl (self.ep, EPOLL_CTL_MOD, poller->fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_reset_out (struct sp_poller *poller)
{
    int rc;
    int i;
    struct epoll_event ev;

    /*  If not polling for OUT, do nothing. */
    if (sp_slow (!(poller->events & EPOLLOUT)))
        return;

    /*  Stop polling for OUT. */
    poller->events &= ~EPOLLOUT;
    memset (&ev, 0, sizeof (ev));
    ev.events = poller->events;
    ev.data.ptr = (void*) poller;
    rc = epoll_ctl (self.ep, EPOLL_CTL_MOD, poller->fd, &ev);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent OUT events on this file descriptor. */
    for (i = self.index; i != self.nevents; ++i)
        if (self.events [i].data.ptr == poller)
            self.events [i].events &= ~EPOLLOUT;
}

static void sp_poller_routine (void *arg)
{
    int rc;
    int timeout;
    struct sp_poller *poller;

    while (1) {

        /*  Wait for both network events and async operation requests. */
        self.nevents = epoll_wait (self.ep, self.events,
            SP_POLLER_MAX_EVENTS, -1);
        if (sp_slow (self.nevents == -1 && errno == EINTR))
            continue;
        if (self.nevents == 0)
            continue;
        errno_assert (self.nevents != -1);

        for (self.index = 0; self.index != self.nevents; ++self.index) {

            /*  If required, terminate the worker thread. */
            if (self.events [self.index].events & EPOLLIN &&
                  self.events [self.index].data.ptr == NULL)
                return;

            /*  Network events. Invoke appropriate callbacks. */
            poller = (struct sp_poller*) self.events [self.index].data.ptr;
            if (self.events [self.index].events & EPOLLOUT)
                poller->vfptr->out (poller);
            if (self.events [self.index].events & EPOLLIN)
                poller->vfptr->in (poller);
            if (self.events [self.index].events & ~(EPOLLIN | EPOLLOUT))
                poller->vfptr->err (poller);
        }
    }
}

#endif

