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

#include "alloc.h"
#include "err.h"

#define SP_POLLER_GRANULARITY 16

void sp_poller_init (struct sp_poller *self)
{
    self->size = 0;
    self->index = 0;
    self->capacity = SP_POLLER_GRANULARITY;
    self->pollset =
        sp_alloc (sizeof (struct pollfd*) * SP_POLLER_GRANULARITY,
            "pollset");
    alloc_assert (self->pollset);
    self->hndls =
        sp_alloc (sizeof (struct sp_hndls_item) * SP_POLLER_GRANULARITY,
            "hndlset");
    alloc_assert (self->hndls);
    self->removed = -1;
}

void sp_poller_term (struct sp_poller *self)
{
    sp_free (self->pollset);
    sp_free (self->hndls);
}

void sp_poller_add (struct sp_poller *self, int fd,
    struct sp_poller_hndl *hndl)
{
    int rc;

    /*  If the capacity is too low to accommodate the next item, resize it. */
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

void sp_poller_rm (struct sp_poller *self, struct sp_poller_hndl *hndl)
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

