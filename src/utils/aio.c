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

#include "aio.h"
#include "err.h"
#include "cont.h"

/*  Private functions. */
static void sp_aio_worker (void *arg);

void sp_aio_init (struct sp_aio *self, const struct sp_aio_vfptr *vfptr)
{
    self->vfptr = vfptr;
    sp_mutex_init (&self->sync, 0);
    sp_timer_init (&self->timer);
    sp_eventfd_init (&self->efd);
    sp_poller_init (&self->poller);
    sp_mutex_init (&self->events_sync, 0);
    sp_list_init (&self->events);

    /*  Make poller listen on the internal eventfd object. */
    sp_poller_add (&self->poller, sp_eventfd_getfd (&self->efd),
        &self->efd_hndl);
    sp_poller_set_in (&self->poller, &self->efd_hndl);

    /*  Launch the worker thread. */
    self->stop = 0;
    sp_thread_init (&self->worker, sp_aio_worker, self);
}

void sp_aio_term (struct sp_aio *self)
{
    /*  Ask worker thread to terminate. */
    sp_mutex_lock (&self->sync);
    self->stop = 1;
    sp_eventfd_signal (&self->efd);
    sp_mutex_unlock (&self->sync);

    /*  Wait till it terminates. */
    sp_thread_term (&self->worker);

    /*  Remove the remaining internal fd from the poller. */
    sp_poller_rm (&self->poller, &self->efd_hndl);

    /*  Deallocate the resources. */
    sp_list_term (&self->events);
    sp_mutex_term (&self->events_sync);
    sp_poller_term (&self->poller);
    sp_eventfd_term (&self->efd);
    sp_timer_term (&self->timer);
    sp_mutex_term (&self->sync);
}

void sp_aio_lock (struct sp_aio *self)
{
    sp_mutex_lock (&self->sync);
}

void sp_aio_unlock (struct sp_aio *self)
{
    sp_mutex_unlock (&self->sync);
}

void sp_aio_add_timer (struct sp_aio *self, int timeout,
    struct sp_timer_hndl *hndl)
{
    int rc;

    rc = sp_timer_add (&self->timer, timeout, hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->worker))
        sp_eventfd_signal (&self->efd);
}

void sp_aio_rm_timer (struct sp_aio *self, struct sp_timer_hndl *hndl)
{
    int rc;

    rc = sp_timer_rm (&self->timer, hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->worker))
        sp_eventfd_signal (&self->efd);
}

void sp_aio_post (struct sp_aio *self, int event, struct sp_event_hndl *hndl)
{
    /*  In case the event is fired from inside of the worker thread we can
        invoke the handler straight away. */
    if (sp_fast (sp_thread_current (&self->worker))) {
        self->vfptr->event (self, event, hndl);
        return;
    }

    /*  Othrewise enqueue the event for later processing. */
    hndl->event = event;
    sp_mutex_lock (&self->events_sync);
    sp_list_insert (&self->events, &hndl->list, sp_list_end (&self->events));
    sp_mutex_unlock (&self->events_sync);
    sp_eventfd_signal (&self->efd);
}

static void sp_aio_worker (void *arg)
{
    int rc;
    struct sp_aio *self;
    int timeout;
    struct sp_timer_hndl *thndl;
    int event;
    struct sp_poller_hndl *phndl;
    struct sp_event_hndl *ehndl;

    self = (struct sp_aio*) arg;

    sp_mutex_lock (&self->sync);

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = sp_timer_timeout (&self->timer);

        /*  Wait for new events and/or timeouts. */
        sp_mutex_unlock (&self->sync);
again:
        rc = sp_poller_wait (&self->poller, timeout);
if (rc == -EINTR)
    goto again;
        errnum_assert (rc == 0, -rc);
        sp_mutex_lock (&self->sync);

        /*  Termination of the worker thread. */
        if (self->stop) {
            sp_mutex_unlock (&self->sync);
            break;
        }

        /*  Process any expired timers. */
        while (1) {
            rc = sp_timer_event (&self->timer, &thndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  Fire the timeout event. */
            self->vfptr->timeout (self, thndl);
        }

        /*  Process any events from the poller. */
        while (1) {
            rc = sp_poller_event (&self->poller, &event, &phndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  The events delivered through the internal eventfd object require
                no action in response. Their sole intent is to interrupt the
                waiting. */
            if (phndl == &self->efd_hndl) {
                sp_assert (event == SP_POLLER_IN);
                continue;
            }

            /*  Process the I/O event. */
            self->vfptr->io (self, event, phndl);
        }

        /*  Process any external events. */
        sp_mutex_lock (&self->events_sync);
        while (1) {
            if (sp_list_empty (&self->events))
                break;
            ehndl = sp_cont (sp_list_begin (&self->events),
                struct sp_event_hndl, list);
            sp_list_erase (&self->events, sp_list_begin (&self->events));
            self->vfptr->event (self, ehndl->event, ehndl);
        }
        sp_mutex_unlock (&self->events_sync);
    }
}

