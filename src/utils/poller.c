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

#include "alloc.h"
#include "err.h"

#define SP_POLLER_GRANULARITY 16

/*  Forward declarations. */
static void sp_poller_routine (void *arg);

void sp_poller_init (struct sp_poller *self, struct sp_poller_vfptr *vfptr)
{
    /*  Store the callback functions. */
    self->vfptr = vfptr;

    self->size = 1;
    self->capacity = SP_POLLER_GRANULARITY;
    self->pollset =
        sp_alloc (sizeof (struct pollfd*) * SP_POLLER_GRANULARITY);
    alloc_assert (self->pollset);
    self->pollset [0].fd = sp_signaler_fd (&self->signaler);
    self->pollset [0].events = POLLIN;
    self->hndls =
        sp_alloc (sizeof (struct sp_hndls_item) * SP_POLLER_GRANULARITY);
    alloc_assert (self->hndls);
    self->hndls [0].hndl = NULL;
    self->removed = -1;

    /*  Start the worker thread. */
    sp_signaler_init (&self->signaler);
    sp_thread_init (&self->worker, sp_poller_routine, (void*) self);
}

void sp_poller_term (struct sp_poller *self)
{
    /*  Ask the worker thread to terminate. */
    sp_signaler_post (&self->signaler);

    /*  Wait while worker thread terminates. */
    sp_thread_term (&self->worker);
    sp_signaler_term (&self->signaler);

    /*  Deallocate the resources. */
    sp_free (self->pollset);
    sp_free (self->hndls);
}

void sp_poller_add_fd (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl)
{
    int rc;

    /*  Initialise the handle and add the file descriptor to the pollset. */
    if (sp_slow (self->size >= self->capacity)) {
        self->capacity += SP_POLLER_GRANULARITY;
        self->pollset = sp_realloc (self->pollset,
            sizeof (struct pollfd*) * self->capacity);
        alloc_assert (self->pollset);
        self->hndls = sp_realloc (self->hndls,
            sizeof (struct sp_hndls_item) * self->capacity);
        alloc_assert (self->hndls);
    }
    self->pollset [self->size].fd = fd;
    self->pollset [self->size].events = 0;
    self->pollset [self->size].revents = 0;
    hndl->index = self->size;
    self->hndls [self->size].hndl = hndl;
    ++self->size;
}

void sp_poller_rm_fd (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    /*  Add the fd to the list of removed file descriptors. */
    self->pollset [hndl->index].revents = 0;
    if (self->removed != -1)
        self->hndls [self->removed].prev = hndl->index;
    self->hndls [hndl->index].hndl = NULL;
    self->hndls [hndl->index].prev = -1;
    self->hndls [hndl->index].next = self->removed;
    self->removed = hndl->index;
}

void sp_poller_set_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    self->pollset [hndl->index].events |= POLLIN;
}

void sp_poller_reset_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    self->pollset [hndl->index].events &= ~POLLIN;
    self->pollset [hndl->index].revents &= ~POLLIN;
}

void sp_poller_set_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    self->pollset [hndl->index].events |= POLLOUT;
}

void sp_poller_reset_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    self->pollset [hndl->index].events &= ~POLLOUT;
    self->pollset [hndl->index].revents &= ~POLLOUT;
}

static void sp_poller_routine (void *arg)
{
    struct sp_poller *self;
    int rc;
    int timeout;
    int i;
    struct sp_poller_hndl *hndl;

    self = (struct sp_poller*) arg;

    while (1) {

        /*  Wait for both network events and async operation requests. */
        rc = poll (self->pollset, self->size, -1);
        if (sp_slow (rc < 0 && errno == EINTR))
            continue;
        if (rc == 0)
            continue;
        errno_assert (rc >= 0);

        /*  If required, terminate the worker thread. */
        if (self->pollset [0].revents & POLLIN)
            break;
        
        for (i = 1; i != self->size; ++i) {

            /*  The descriptor may already have been removed. */
            if (!self->hndls [i].hndl)
                continue;

            /*  Network events. Invoke appropriate callbacks. */
            hndl = self->hndls [i].hndl;
            sp_assert (hndl->index == i);
            if (self->pollset [i].revents & POLLOUT)
                self->vfptr->out (self, hndl);
            if (!self->hndls [i].hndl)
                continue;
            if (self->pollset [i].revents & POLLIN)
                self->vfptr->in (self, hndl);
            if (!self->hndls [i].hndl)
                continue;
            if (self->pollset [i].revents & ~(POLLIN | POLLOUT))
                self->vfptr->err (self, hndl);
        }

        /*  Physically delete all the previosuly removed file descriptors from
            the pollset. */
        while (self->removed != -1) {
            i = self->removed;
            self->removed = self->hndls [i].next;
            self->pollset [i] = self->pollset [self->size - 1];
            self->hndls [i] = self->hndls [self->size - 1];
            self->hndls [i].hndl->index = i;

            if (!self->hndls [i].hndl) {
                if (self->hndls [i].prev != -1)
                   self->hndls [self->hndls [i].prev].next = i;
                if (self->hndls [i].next != -1)
                   self->hndls [self->hndls [i].next].prev = i;
                if (self->removed == self->size - 1)
                    self->removed = i;
            }
            --self->size;
        }
    }
}

#elif defined SP_USE_EPOLL

#include "err.h"

#include <string.h>

void sp_poller_init (struct sp_poller *self, struct sp_poller_vfptr *vfptr)
{
    int rc;
    struct epoll_event ev;

    self->ep = epoll_create1 (EPOLL_CLOEXEC);
    errno_assert (self->ep != -1);
    memset (&ev, 0, sizeof (ev));
    ev.events = EPOLLIN;
    rc = epoll_ctl (self->ep, EPOLL_CTL_ADD,
        sp_signaler_fd (&self->signaler), &ev);
    errno_assert (rc == 0);
}

void sp_poller_term (struct sp_poller *self)
{
    int rc;

    rc = close (self->ep);
    errno_assert (rc == 0);
}

void sp_poller_add_fd (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl)
{
    int rc;
    struct epoll_event ev;

    /*  Initialise the handle and add the file descriptor to the pollset. */
    hndl->fd = fd;
    hndl->events = 0;
    memset (&ev, 0, sizeof (ev));
    ev.events = 0;
    ev.data.ptr = (void*) hndl;
    rc = epoll_ctl (self->ep, EPOLL_CTL_ADD, fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_rm_fd (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    int i;

    /*  Remove the file descriptor from the pollset. */
    rc = epoll_ctl (self->ep, EPOLL_CTL_DEL, hndl->fd, NULL);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].data.ptr == hndl)
            self->events [i].events = 0;
}

void sp_poller_set_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct epoll_event ev;

    /*  If already polling for IN, do nothing. */
    if (sp_slow (hndl->events & EPOLLIN))
        return;

    /*  Start polling for IN. */
    hndl->events |= EPOLLIN;
    memset (&ev, 0, sizeof (ev));
    ev.events = hndl->events;
    ev.data.ptr = (void*) hndl;
    rc = epoll_ctl (self->ep, EPOLL_CTL_MOD, hndl->fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_reset_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    int i;
    struct epoll_event ev;

    /*  If not polling for IN, do nothing. */
    if (sp_slow (!(hndl->events & EPOLLIN)))
        return;

    /*  Stop polling for IN. */
    hndl->events & ~EPOLLIN;
    memset (&ev, 0, sizeof (ev));
    ev.events = hndl->events;
    ev.data.ptr = (void*) hndl;
    rc = epoll_ctl (self->ep, EPOLL_CTL_MOD, hndl->fd, &ev);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent IN events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].data.ptr == hndl)
            self->events [i].events &= ~EPOLLIN;
}

void sp_poller_set_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct epoll_event ev;

    /*  If already polling for OUT, do nothing. */
    if (sp_slow (hndl->events & EPOLLOUT))
        return;

    /*  Start polling for OUT. */
    hndl->events |= EPOLLOUT;
    memset (&ev, 0, sizeof (ev));
    ev.events = hndl->events;
    ev.data.ptr = (void*) hndl;
    rc = epoll_ctl (self->ep, EPOLL_CTL_MOD, hndl->fd, &ev);
    errno_assert (rc == 0);
}

void sp_poller_reset_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    int i;
    struct epoll_event ev;

    /*  If not polling for OUT, do nothing. */
    if (sp_slow (!(hndl->events & EPOLLOUT)))
        return;

    /*  Stop polling for OUT. */
    hndl->events &= ~EPOLLOUT;
    memset (&ev, 0, sizeof (ev));
    ev.events = hndl->events;
    ev.data.ptr = (void*) hndl;
    rc = epoll_ctl (self->ep, EPOLL_CTL_MOD, hndl->fd, &ev);
    errno_assert (rc == 0);

    /*  Invalidate any subsequent OUT events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].data.ptr == hndl)
            self->events [i].events &= ~EPOLLOUT;
}

static void sp_poller_routine (void *arg)
{
    struct sp_poller *self;
    int rc;
    int timeout;
    struct sp_poller_hndl *hndl;

    self = (struct sp_poller*) arg;

    while (1) {

        /*  Wait for both network events and async operation requests. */
        self->nevents = epoll_wait (self->ep, self->events,
            SP_POLLER_MAX_EVENTS, -1);
        if (sp_slow (self->nevents == -1 && errno == EINTR))
            continue;
        if (self->nevents == 0)
            continue;
        errno_assert (self->nevents != -1);

        for (self->index = 0; self->index != self->nevents; ++self->index) {

            /*  If required, terminate the worker thread. */
            if (self->events [self->index].events & EPOLLIN &&
                  self->events [self->index].data.ptr == NULL)
                return;

            /*  Network events. Invoke appropriate callbacks. */
            hndl = (struct sp_poller_hndl*) self->events [self->index].data.ptr;
            if (self->events [self->index].events & EPOLLOUT)
                self->vfptr->out (self, hndl);
            if (self->events [self->index].events & EPOLLIN)
                self->vfptr->in (self, hndl);
            if (self->events [self->index].events & ~(EPOLLIN | EPOLLOUT))
                self->vfptr->err (self, hndl);
        }
    }
}

#else
#error
#endif

