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

void sp_poller_init (struct sp_poller *self)
{
    self->size = 0;
    self->index = 0;
    self->capacity = SP_POLLER_GRANULARITY;
    self->pollset =
        sp_alloc (sizeof (struct pollfd*) * SP_POLLER_GRANULARITY);
    alloc_assert (self->pollset);
    self->hndls =
        sp_alloc (sizeof (struct sp_hndls_item) * SP_POLLER_GRANULARITY);
    alloc_assert (self->hndls);
    self->removed = -1;
}

void sp_poller_term (struct sp_poller *self)
{
    sp_free (self->pollset);
    sp_free (self->hndls);
}

void sp_poller_add_fd (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl)
{
    int rc;

    /*  If the capacity is too low to accomodate the next item, resize it. */
    if (sp_slow (self->size >= self->capacity)) {
        self->capacity *= 2;
        self->pollset = sp_realloc (self->pollset,
            sizeof (struct pollfd*) * self->capacity);
        alloc_assert (self->pollset);
        self->hndls = sp_realloc (self->hndls,
            sizeof (struct sp_hndls_item) * self->capacity);
        alloc_assert (self->hndls);
    }

    /*  Add the fd to the pollset. */
    self->pollset [self->size].fd = fd;
    self->pollset [self->size].events = 0;
    self->pollset [self->size].revents = 0;
    hndl->index = self->size;
    self->hndls [self->size].hndl = hndl;
    ++self->size;
}

void sp_poller_rm_fd (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    /*  No more events will be reported on this fd. */
    self->pollset [hndl->index].revents = 0;

    /*  Add the fd into the list of removed fds. */
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

int sp_poller_wait (struct sp_poller *self, int timeout)
{
    int rc;
    int i;

    /*  First, get rid of removed fds. */
    while (self->removed != -1) {

        /*  Remove the fd from the list of removed fds. */
        i = self->removed;
        self->removed = self->hndls [i].next;

        /*  Replace the removed fd by the one at the end of the pollset. */
        --self->size;
        if (i != self->size) { 
            self->pollset [i] = self->pollset [self->size];
            self->hndls [i] = self->hndls [self->size];
            self->hndls [i].hndl->index = i;
        }

        /*  The fd from the end of the pollset may have been on removed fds
            list itself. If so, adjust the list. */
        if (sp_slow (!self->hndls [i].hndl)) {
            if (self->hndls [i].prev != -1)
               self->hndls [self->hndls [i].prev].next = i;
            if (self->hndls [i].next != -1)
               self->hndls [self->hndls [i].next].prev = i;
            if (self->removed == self->size)
                self->removed = i;
        }
    }

    /*  Wait for new events. */
#if defined SP_IGNORE_EINTR
again:
#endif
    rc = poll (self->pollset, self->size, timeout);
    if (sp_slow (rc < 0 && errno == EINTR))
#if defined SP_IGNORE_EINTR
        goto again;
#else
        return -EINTR;
#endif
    errno_assert (rc >= 0);
    return 0;
}

int sp_poller_event (struct sp_poller *self, int *event,
    struct sp_poller_hndl **hndl)
{
    int rc;

    /*  Skip over empty events. This will also skip over removed fds as they
        have their revents nullified. */
    while (self->index < self->size) {
        if (self->pollset [self->index].revents != 0)
            break;
        ++self->index;
    }

    /*  If there is no available event, let the caller know. */
    if (sp_slow (self->index >= self->size))
        return -EAGAIN;

    /*  Return next event to the caller. Remove the event from revents. */
    *hndl = self->hndls [self->index].hndl;
    if (sp_fast (self->pollset [self->index].revents & POLLIN)) {
        *event = SP_POLLER_IN;
        self->pollset [self->index].revents &= ~POLLIN;
        return 0;
    }
    else if (sp_fast (self->pollset [self->index].revents & POLLOUT)) {
        *event = SP_POLLER_OUT;
        self->pollset [self->index].revents &= ~POLLOUT;
        return 0;
    }
    else {
        *event = SP_POLLER_ERR;
        ++self->index;
        return 0;
    }
}

#endif

#if defined SP_USE_EPOLL

#include "fast.h"
#include "err.h"

#include <string.h>

void sp_poller_init (struct sp_poller *self)
{
    int rc;
    struct epoll_event ev;

    self->ep = epoll_create1 (EPOLL_CLOEXEC);
    errno_assert (self->ep != -1);
    self->nevents = 0;
    self->index = 0;
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

int sp_poller_wait (struct sp_poller *self, int timeout)
{
    int nevents;

    /*  Clear all existing events. */
    self->nevents = 0;
    self->index = 0;

    /*  Wait for new events. */
#if defined SP_IGNORE_EINTR
again:
#endif
    nevents = epoll_wait (self->ep, self->events,
        SP_POLLER_MAX_EVENTS, timeout);
    if (sp_slow (nevents == -1 && errno == EINTR))
#if defined SP_IGNORE_EINTR
        goto again;
#else
        return -EINTR;
#endif
    errno_assert (self->nevents != -1);
    self->nevents = nevents;
    return 0;
}

int sp_poller_event (struct sp_poller *self, int *event,
    struct sp_poller_hndl **hndl)
{
    /*  Skip over empty events. */
    while (self->index < self->nevents) {
        if (self->events [self->index].events != 0)
            break;
        ++self->index;
    }

    /*  If there is no stored event, let the caller know. */
    if (sp_slow (self->index >= self->nevents))
        return -EAGAIN;

    /*  Return next event to the caller. Remove the event from the set. */
    *hndl = (struct sp_poller_hndl*) self->events [self->index].data.ptr;
    if (sp_fast (self->events [self->index].events & EPOLLIN)) {
        *event = SP_POLLER_IN;
        self->events [self->index].events &= ~EPOLLIN;
        return 0;
    }
    else if (sp_fast (self->events [self->index].events & EPOLLOUT)) {
        *event = SP_POLLER_OUT;
        self->events [self->index].events &= ~EPOLLOUT;
        return 0;
    }
    else {
        *event = SP_POLLER_ERR;
        ++self->index;
        return 0;
    }
}

#endif

