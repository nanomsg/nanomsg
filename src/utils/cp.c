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

#include "cp.h"
#include "err.h"
#include "cont.h"
#include "fast.h"

#include <sys/socket.h>

/*  Private functions. */
static void sp_cp_worker (void *arg);
static int sp_cp_send_raw (int s, const void *buf, size_t *len);
static int sp_cp_recv_raw (int s, void *buf, size_t *len);

void sp_cp_init (struct sp_cp *self, const struct sp_cp_vfptr *vfptr)
{
    self->vfptr = vfptr;
    sp_mutex_init (&self->sync, 0);
    sp_timer_init (&self->timer);
    sp_efd_init (&self->efd);
    sp_poller_init (&self->poller);
    sp_queue_init (&self->opqueue);
    sp_mutex_init (&self->events_sync, 0);
    sp_queue_init (&self->events);

    /*  Make poller listen on the internal efd object. */
    sp_poller_add (&self->poller, sp_efd_getfd (&self->efd),
        &self->efd_hndl);
    sp_poller_set_in (&self->poller, &self->efd_hndl);

    /*  Launch the worker thread. */
    self->stop = 0;
    sp_thread_init (&self->worker, sp_cp_worker, self);
}

void sp_cp_term (struct sp_cp *self)
{
    /*  Ask worker thread to terminate. */
    sp_mutex_lock (&self->sync);
    self->stop = 1;
    sp_efd_signal (&self->efd);
    sp_mutex_unlock (&self->sync);

    /*  Wait till it terminates. */
    sp_thread_term (&self->worker);

    /*  Remove the remaining internal fd from the poller. */
    sp_poller_rm (&self->poller, &self->efd_hndl);

    /*  Deallocate the resources. */
    sp_queue_term (&self->opqueue);
    sp_queue_term (&self->events);
    sp_mutex_term (&self->events_sync);
    sp_poller_term (&self->poller);
    sp_efd_term (&self->efd);
    sp_timer_term (&self->timer);
    sp_mutex_term (&self->sync);
}

void sp_cp_lock (struct sp_cp *self)
{
    sp_mutex_lock (&self->sync);
}

void sp_cp_unlock (struct sp_cp *self)
{
    sp_mutex_unlock (&self->sync);
}

void sp_cp_add_timer (struct sp_cp *self, int timeout,
    const struct sp_cp_timer_vfptr *vfptr, struct sp_cp_timer_hndl *hndl)
{
    int rc;

    hndl->vfptr = vfptr;
    rc = sp_timer_add (&self->timer, timeout, &hndl->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->worker))
        sp_efd_signal (&self->efd);
}

void sp_cp_rm_timer (struct sp_cp *self, struct sp_cp_timer_hndl *hndl)
{
    int rc;

    rc = sp_timer_rm (&self->timer, &hndl->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !sp_thread_current (&self->worker))
        sp_efd_signal (&self->efd);
}

void sp_cp_post (struct sp_cp *self, int event, struct sp_event_hndl *hndl)
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
    sp_queue_push (&self->events, &hndl->item);
    sp_mutex_unlock (&self->events_sync);
    sp_efd_signal (&self->efd);
}

void sp_cp_add_fd (struct sp_cp *self, int s,
    const struct sp_cp_io_vfptr *vfptr, struct sp_cp_io_hndl *hndl)
{
    hndl->vfptr = vfptr;
    hndl->s = s;
    hndl->in.op = SP_CP_INOP_NONE;
    hndl->out.op = SP_CP_OUTOP_NONE;

    /*  Initialise op types. */
    hndl->add_hndl.op = SP_CP_OP_ADD;
    hndl->rm_hndl.op = SP_CP_OP_RM;
    hndl->in.hndl.op = SP_CP_OP_IN;
    hndl->out.hndl.op = SP_CP_OP_OUT;

    if (sp_thread_current (&self->worker)) {
        sp_poller_add (&self->poller, s, &hndl->hndl);
        return;
    }

    /*  Send an event to the worker thread. */
    sp_queue_push (&self->opqueue, &hndl->add_hndl.item);
    sp_efd_signal (&self->efd);
}

void sp_cp_rm_fd (struct sp_cp *self, struct sp_cp_io_hndl *hndl)
{
    if (sp_thread_current (&self->worker)) {
        sp_poller_rm (&self->poller, &hndl->hndl);
        return;
    }

    /*  Send an event to the worker thread. */
    sp_queue_push (&self->opqueue, &hndl->rm_hndl.item);
    sp_efd_signal (&self->efd);
}

void sp_cp_pollin (struct sp_cp *self, struct sp_cp_io_hndl *hndl)
{
    /*  Make sure that there's no inbound operation already in progress. */
    sp_assert (hndl->in.op == SP_CP_INOP_NONE);

    /*  Adjust the handle. */
    hndl->in.op = SP_CP_INOP_POLLIN;

    /*  If we are in the worker thread we can simply start polling for out. */
    if (sp_thread_current (&self->worker)) {
        sp_poller_set_in (&self->poller, &hndl->hndl);
        return;
    }

    /*  Otherwise, ask worker thread to start polling for in. */
    sp_queue_push (&self->opqueue, &hndl->in.hndl.item);
    sp_efd_signal (&self->efd);
}

void sp_cp_pollout (struct sp_cp *self, struct sp_cp_io_hndl *hndl)
{
    /*  Make sure that there's no outbound operation already in progress. */
    sp_assert (hndl->out.op == SP_CP_OUTOP_NONE);

    /*  Adjust the handle. */
    hndl->out.op = SP_CP_OUTOP_POLLOUT;

    /*  If we are in the worker thread we can simply start polling for out. */
    if (sp_thread_current (&self->worker)) {
        sp_poller_set_out (&self->poller, &hndl->hndl);
        return;
    }

    /*  Otherwise, ask worker thread to start polling for out. */
    sp_queue_push (&self->opqueue, &hndl->out.hndl.item);
    sp_efd_signal (&self->efd);
}

int sp_cp_send (struct sp_cp *self, struct sp_cp_io_hndl *hndl, const void *buf,
    size_t *len, int flags)
{
    int rc;
    size_t sz;

    /*  Make sure that there's no outbound operation already in progress. */
    sp_assert (hndl->out.op == SP_CP_OUTOP_NONE);

    /*  Try to send the data immediately. */
    sz = *len;
    rc = sp_cp_send_raw (hndl->s, buf, len);
    if (sp_slow (rc < 0))
        return rc;

    /*  Success. */
    if (sp_fast ((flags & SP_CP_PARTIAL && *len > 0) || *len == sz))
        return 0;

    /*  There are still data to send in the background. */ 
    hndl->out.op = flags & SP_CP_PARTIAL ? SP_CP_OUTOP_SEND_PARTIAL :
        SP_CP_OUTOP_SEND;
    hndl->out.buf = buf;
    hndl->out.buflen = sz;
    hndl->out.len = *len;

    /*  If we are in the worker thread we can simply start polling for out. */
    if (sp_thread_current (&self->worker)) {
        sp_poller_set_out (&self->poller, &hndl->hndl);
        return -EINPROGRESS;
    }

    /*  Otherwise, ask worker thread to start polling for out. */
    sp_queue_push (&self->opqueue, &hndl->out.hndl.item);
    sp_efd_signal (&self->efd);

    return -EINPROGRESS;
}

int sp_cp_recv (struct sp_cp *self, struct sp_cp_io_hndl *hndl, void *buf,
    size_t *len, int flags)
{
    int rc;
    size_t sz;

    /*  Make sure that there's no inbound operation already in progress. */
    sp_assert (hndl->in.op == SP_CP_INOP_NONE);

    /*  Try to receive the data immediately. */
    sz = *len;
    rc = sp_cp_recv_raw (hndl->s, buf, len);
    if (sp_slow (rc < 0))
        return rc;

    /*  Success. */
    if (sp_fast ((flags & SP_CP_PARTIAL && *len > 0) || *len == sz))
        return 0;

    /*  There are still data to receive in the background. */ 
    hndl->in.op = flags & SP_CP_PARTIAL ? SP_CP_INOP_RECV_PARTIAL :
        SP_CP_INOP_RECV;
    hndl->in.buf = buf;
    hndl->in.buflen = sz;
    hndl->in.len = *len;

    /*  If we are in the worker thread we can simply start polling for in. */
    if (sp_thread_current (&self->worker)) {
        sp_poller_set_in (&self->poller, &hndl->hndl);
        return -EINPROGRESS;
    }

    /*  Otherwise, ask worker thread to start polling for in. */
    sp_queue_push (&self->opqueue, &hndl->in.hndl.item);
    sp_efd_signal (&self->efd);

    return -EINPROGRESS;
}

static void sp_cp_worker (void *arg)
{
    int rc;
    struct sp_cp *self;
    int timeout;
    struct sp_op_hndl *ohndl;
    struct sp_timer_hndl *thndl;
    struct sp_cp_timer_hndl *cthndl;
    int event;
    struct sp_poller_hndl *phndl;
    struct sp_event_hndl *ehndl;
    struct sp_cp_io_hndl *ihndl;
    size_t sz;

    self = (struct sp_cp*) arg;

    sp_mutex_lock (&self->sync);

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = sp_timer_timeout (&self->timer);

        /*  Wait for new events and/or timeouts. */
        sp_mutex_unlock (&self->sync);
again:
        rc = sp_poller_wait (&self->poller, timeout);
if (rc == -EINTR) goto again;
        errnum_assert (rc == 0, -rc);
        sp_mutex_lock (&self->sync);

        /*  Termination of the worker thread. */
        if (self->stop) {
            sp_mutex_unlock (&self->sync);
            break;
        }

        /*  Process the events in the opqueue. */
        while (1) {

            ohndl = sp_cont (sp_queue_pop (&self->opqueue),
                struct sp_op_hndl, item);
            if (!ohndl)
                break;

            switch (ohndl->op) {
            case SP_CP_OP_IN:
                ihndl = sp_cont (ohndl, struct sp_cp_io_hndl, in.hndl);
                sp_poller_set_in (&self->poller, &ihndl->hndl);
                break;
            case SP_CP_OP_OUT:
                ihndl = sp_cont (ohndl, struct sp_cp_io_hndl, out.hndl);
                sp_poller_set_out (&self->poller, &ihndl->hndl);
                break;
            case SP_CP_OP_ADD:
                ihndl = sp_cont (ohndl, struct sp_cp_io_hndl, add_hndl);
                sp_poller_add (&self->poller, ihndl->s, &ihndl->hndl);
                break;
            case SP_CP_OP_RM:
                ihndl = sp_cont (ohndl, struct sp_cp_io_hndl, rm_hndl);
                sp_poller_rm (&self->poller, &ihndl->hndl);
                break;
            default:
                sp_assert (0);
            }
        }

        /*  Process any expired timers. */
        while (1) {
            rc = sp_timer_event (&self->timer, &thndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  Fire the timeout event. */
            cthndl = sp_cont (thndl, struct sp_cp_timer_hndl, hndl);
            cthndl->vfptr->timeout (cthndl);
        }

        /*  Process any events from the poller. */
        while (1) {
            rc = sp_poller_event (&self->poller, &event, &phndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  The events delivered through the internal efd object require
                no action in response. Their sole intent is to interrupt the
                waiting. */
            if (phndl == &self->efd_hndl) {
                sp_assert (event == SP_POLLER_IN);
                continue;
            }

            /*  Process the I/O event. */
            ihndl = sp_cont (phndl, struct sp_cp_io_hndl ,hndl);
            switch (event) {
            case SP_POLLER_IN:
                switch (ihndl->in.op) {
                case SP_CP_INOP_RECV:
                case SP_CP_INOP_RECV_PARTIAL:
                    sz = ihndl->in.buflen - ihndl->in.len;
                    rc = sp_cp_recv_raw (ihndl->s, ((char*) ihndl->in.buf) +
                        ihndl->in.len, &sz);
                    if (rc < 0)
                        goto err;
                    ihndl->in.len += sz;
                    if (ihndl->in.op == SP_CP_INOP_RECV_PARTIAL ||
                          ihndl->in.len == ihndl->in.buflen) {
                        ihndl->in.op = SP_CP_INOP_NONE;
                        sp_poller_reset_in (&self->poller, &ihndl->hndl);
                        ihndl->vfptr->in (ihndl);
                    }
                    break;
                case SP_CP_INOP_POLLIN:
                    ihndl->in.op = SP_CP_INOP_NONE;
                    sp_poller_reset_in (&self->poller, &ihndl->hndl);
                    ihndl->vfptr->in (ihndl);
                    break;
                default:
                    sp_assert (0);
                }
            case SP_POLLER_OUT:
                switch (ihndl->out.op) {
                case SP_CP_OUTOP_SEND:
                case SP_CP_OUTOP_SEND_PARTIAL:
                    sz = ihndl->out.buflen - ihndl->out.len;
                    rc = sp_cp_send_raw (ihndl->s, ((char*) ihndl->out.buf) +
                        ihndl->out.len, &sz);
                    if (rc < 0)
                        goto err;
                    ihndl->out.len += sz;
                    if (ihndl->out.op == SP_CP_OUTOP_SEND_PARTIAL ||
                          ihndl->out.len == ihndl->out.buflen) {
                        ihndl->out.op = SP_CP_OUTOP_NONE;
                        sp_poller_reset_out (&self->poller, &ihndl->hndl);
                        ihndl->vfptr->out (ihndl);
                    }
                    break;
                case SP_CP_OUTOP_POLLOUT:
                    ihndl->out.op = SP_CP_OUTOP_NONE;
                    sp_poller_reset_out (&self->poller, &ihndl->hndl);
                    ihndl->vfptr->out (ihndl);
                    break;
                default:
                    sp_assert (0);
                }
            case SP_POLLER_ERR:
err:
                sp_assert (0);
            default:
                sp_assert (0);
            }
        }

        /*  Process any external events. */
        sp_mutex_lock (&self->events_sync);
        while (1) {
            ehndl = sp_cont (sp_queue_pop (&self->events),
                struct sp_event_hndl, item);
            if (!ehndl)
                break;
            self->vfptr->event (self, ehndl->event, ehndl);
        }
        sp_mutex_unlock (&self->events_sync);
    }
}

static int sp_cp_send_raw (int s, const void *buf, size_t *len)
{
    ssize_t nbytes;

#if defined MSG_NOSIGNAL
    nbytes = send (s, buf, *len, MSG_NOSIGNAL);
#else
    nbytes = send (s, buf, *len, 0);
#endif

    /*  Success. */
    if (sp_fast (nbytes >= 0)) {
        *len = (size_t) nbytes;
        return 0;
    }

    /*  If the connection fails, return ECONNRESET. */
    sp_assert (errno == ECONNRESET || errno == ETIMEDOUT || errno == EPIPE);
    return -ECONNRESET;
}

static int sp_cp_recv_raw (int s, void *buf, size_t *len)
{
    ssize_t nbytes;

    nbytes = recv (s, buf, *len, 0);

    /*  Success. */
    if (sp_fast (nbytes > 0)) {
        *len = (size_t) nbytes;
        return 0;
    }

    /*  If the peer closes the connection, return ECONNRESET. */
    sp_assert (nbytes == 0 || errno == ECONNRESET || errno == ENOTCONN ||
          errno == ECONNREFUSED || errno == ETIMEDOUT ||
          errno == EHOSTUNREACH);
    return -ECONNRESET;
}

