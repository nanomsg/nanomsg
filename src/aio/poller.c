/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright (c) 2015-2016 Jack R. Dunaway.  All rights reserved.
    Copyright 2016 Franklin "Snaipe" Mathieu <franklinmathieu@gmail.com>

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

#if defined NN_USE_EPOLL
    #include "poller_epoll.inc"
#elif defined NN_USE_KQUEUE
    #include "poller_kqueue.inc"
#elif defined NN_USE_POLL
    #include "poller_poll.inc"
#else
    #error
#endif

#if !defined NN_USE_POLL && (defined NN_USE_EPOLL || defined NN_USE_KQUEUE)
#include "../utils/cont.h"

void nn_poller_revive (struct nn_poller *self)
{
    struct nn_list fds;
    struct nn_list_item *it, *next;
    struct nn_poller_hndl *hndl;
    uint32_t events;
    int rc;

    /*  Copy the file descriptor list locally. */
    memcpy (&fds, &self->fds, sizeof (struct nn_list));
    nn_list_init (&self->fds);

    /*  Recreate the poller  */
    rc = nn_poller_reinit (self);
    errnum_assert (rc == 0, rc);

    /*  Add all file descriptors back  */
    for (it = nn_list_begin (&fds); it != nn_list_end (&fds);) {
        next = nn_list_erase (&fds, it);
        nn_list_item_term (it);

        hndl = nn_cont (it, struct nn_poller_hndl, item);

        events = hndl->events;
        nn_poller_add (self, hndl->fd, hndl);

#ifdef NN_USE_EPOLL
        if (events & EPOLLOUT) {
#else
        if (events & NN_POLLER_EVENT_OUT) {
#endif
            nn_poller_set_out (self, hndl);
        }

#ifdef NN_USE_EPOLL
        if (events & EPOLLIN) {
#else
        if (events & NN_POLLER_EVENT_IN) {
#endif
            nn_poller_set_in (self, hndl);
        }

        it = next;
    }
}
#endif
