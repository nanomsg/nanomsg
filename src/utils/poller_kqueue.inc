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

#include <unistd.h>
#include "fast.h"
#include "err.h"

/*  NetBSD has different definition of udata. */
#if defined SP_HAVE_NETBSD
#define sp_poller_udata intptr_t
#else
#define sp_poller_udata void*
#endif

void sp_poller_init (struct sp_poller *self)
{
    self->kq = kqueue ();
    errno_assert (self->kq != -1);
    self->nevents = 0;
    self->index = 0;
}

void sp_poller_term (struct sp_poller *self)
{
    int rc;

    rc = close (self->kq);
    errno_assert (rc == 0);
}

void sp_poller_add (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl)
{
    /*  Initialise the handle. */
    hndl->fd = fd;
    hndl->events = 0;
}

void sp_poller_rm (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct kevent ev;
    int i;

    if (hndl->events & SP_POLLER_EVENT_IN) {
        EV_SET (&ev, hndl->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
    }

    if (hndl->events & SP_POLLER_EVENT_OUT) {
        EV_SET (&ev, hndl->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
    }

    /*  Invalidate any subsequent events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].ident == hndl->fd)
            self->events [i].udata = NULL;
}

void sp_poller_set_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct kevent ev;

    if (!(hndl->events & SP_POLLER_EVENT_IN)) {
        EV_SET (&ev, hndl->fd, EVFILT_READ, EV_ADD, 0, 0,
            (sp_poller_udata) hndl);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
        hndl->events |= SP_POLLER_EVENT_IN;
    }
}

void sp_poller_reset_in (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct kevent ev;
    int i;

    if (hndl->events & SP_POLLER_EVENT_IN) {
        EV_SET (&ev, hndl->fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
        hndl->events &= ~SP_POLLER_EVENT_IN;
    }

    /*  Invalidate any subsequent IN events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].ident == hndl->fd &&
              self->events [i].filter == EVFILT_READ)
            self->events [i].udata = NULL;
}

void sp_poller_set_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct kevent ev;

    if (!(hndl->events & SP_POLLER_EVENT_OUT)) {
        EV_SET (&ev, hndl->fd, EVFILT_WRITE, EV_ADD, 0, 0,
            (sp_poller_udata) hndl);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
        hndl->events |= SP_POLLER_EVENT_IN;
    }
}

void sp_poller_reset_out (struct sp_poller *self, struct sp_poller_hndl *hndl)
{
    int rc;
    struct kevent ev;
    int i;

    if (hndl->events & SP_POLLER_EVENT_OUT) {
        EV_SET (&ev, hndl->fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
        rc = kevent (self->kq, &ev, 1, NULL, 0, NULL);
        errno_assert (rc != -1);
        hndl->events &= ~SP_POLLER_EVENT_OUT;
    }

    /*  Invalidate any subsequent OUT events on this file descriptor. */
    for (i = self->index; i != self->nevents; ++i)
        if (self->events [i].ident == hndl->fd &&
              self->events [i].filter == EVFILT_WRITE)
            self->events [i].udata = NULL;
}

int sp_poller_wait (struct sp_poller *self, int timeout)
{
    struct timespec ts;
    int nevents;

    /*  Clear all existing events. */
    self->nevents = 0;
    self->index = 0;

    /*  Wait for new events. */
#if defined SP_IGNORE_EINTR
again:
#endif
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (timeout % 1000) * 1000000;
    nevents = kevent (self->kq, NULL, 0, &self->events [0],
        SP_POLLER_MAX_EVENTS, timeout >= 0 ? &ts : NULL);
    if (nevents == -1 && errno == EINTR)
#if defined SP_IGNORE_EINTR
        goto again;
#else
        return -EINTR;
#endif
    errno_assert (nevents != -1);

    self->nevents = nevents;
    return 0;
}

int sp_poller_event (struct sp_poller *self, int *event,
    struct sp_poller_hndl **hndl)
{
    /*  Skip over empty events. */
    while (self->index < self->nevents) {
        if (self->events [self->index].udata)
            break;
        ++self->index;
    }

    /*  If there is no stored event, let the caller know. */
    if (sp_slow (self->index >= self->nevents))
        return -EAGAIN;

    /*  Return next event to the caller. Remove the event from the set. */
    *hndl = (struct sp_poller_hndl*) self->events [self->index].udata;
    if (self->events [self->index].flags & EV_EOF)
        *event = SP_POLLER_ERR;
    else if (self->events [self->index].filter == EVFILT_WRITE)
        *event = SP_POLLER_OUT;
    else if (self->events [self->index].filter == EVFILT_READ)
        *event = SP_POLLER_IN;
    else
        sp_assert (0);
    ++self->index;

    return 0;
}

