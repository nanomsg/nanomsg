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

#include "../pattern.h"
#include "../transport.h"

#include "sock.h"

#include "../utils/err.h"
#include "../utils/cont.h"

/*  Private functions. */
void sp_sockbase_timer_routine (struct sp_timer *self);
static void sp_sock_worker_routine (void *arg);

void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    self->vfptr = vfptr;
    sp_mutex_init (&self->sync, 0);
    self->fd = fd;
    sp_mutex_init (&self->delayed_sync, 0);
    sp_cond_init (&self->delayed_cond);
    sp_list_init (&self->delayed_ins);
    sp_list_init (&self->delayed_outs);
    sp_clock_init (&self->clock);
    sp_list_init (&self->timers);
    sp_cp_init (&self->cp);
    sp_thread_init (&self->worker, sp_sock_worker_routine, self);
}

void sp_sock_term (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    /*  Terminate the derived class. */
    sockbase->vfptr->term (sockbase);

    /*  Ask the worker thread to terminate and wait till it does. */
    sp_cp_post (&sockbase->cp, &sockbase->stop);
    sp_thread_term (&sockbase->worker);
    sp_cp_term (&sockbase->cp);
    sp_list_term (&sockbase->timers);
    sp_clock_term (&sockbase->clock);

    /*  Terminate the sp_sockbase itself. */
    sp_list_term (&sockbase->delayed_outs);
    sp_list_term (&sockbase->delayed_ins);
    sp_cond_term (&sockbase->delayed_cond);
    sp_mutex_term (&sockbase->delayed_sync);
    sp_mutex_term (&sockbase->sync);
}

int sp_sock_setopt (struct sp_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->setopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_mutex_unlock (&sockbase->sync);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_mutex_unlock (&sockbase->sync);
    return -ENOPROTOOPT;
}

int sp_sock_getopt (struct sp_sock *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->getopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_mutex_unlock (&sockbase->sync);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_mutex_unlock (&sockbase->sync);
    return -ENOPROTOOPT;
}

int sp_sock_send (struct sp_sock *self, const void *buf, size_t len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;
    struct sp_list_item *it;
    struct sp_pipebase *pipe;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);
    sp_mutex_lock (&sockbase->delayed_sync);

    while (1) {

        /*  Move any delayed outs to the working set of outbound pipes. */
        for (it = sp_list_begin (&sockbase->delayed_outs);
              it != sp_list_end (&sockbase->delayed_outs);
              it = sp_list_next (&sockbase->delayed_outs, it)) {
            pipe = sp_cont (it, struct sp_pipebase, delayed_out);
            sockbase->vfptr->out (sockbase, (struct sp_pipe*) pipe);
        }
        sp_list_clear (&sockbase->delayed_outs);
        sp_mutex_unlock (&sockbase->delayed_sync);

        /*  Try to send the message in a non-blocking way. */
        rc = sockbase->vfptr->send (sockbase, buf, len);
        sp_mutex_unlock (&sockbase->sync);
        if (sp_fast (rc == 0))
            return 0;

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN))
            return rc;

        /*  If the message cannot be sent at the moment and the send call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT))
            return -EAGAIN;

        /*  With blocking send, wait while there are new pipes available
            for sending. */
        sp_mutex_lock (&sockbase->delayed_sync);
        if (sp_list_empty (&sockbase->delayed_outs)) {
            rc = sp_cond_wait (&sockbase->delayed_cond,
                &sockbase->delayed_sync, -1);
            errnum_assert (rc == 0, rc);
        }

        sp_mutex_lock (&sockbase->sync);
    }   
}

int sp_sock_recv (struct sp_sock *self, void *buf, size_t *len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;
    struct sp_list_item *it;
    struct sp_pipebase *pipe;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);
    sp_mutex_lock (&sockbase->delayed_sync);

    while (1) {

        /*  Move any delayed ins to the working set of inbound pipes. */
        for (it = sp_list_begin (&sockbase->delayed_ins);
              it != sp_list_end (&sockbase->delayed_ins);
              it = sp_list_next (&sockbase->delayed_ins, it)) {
            pipe = sp_cont (it, struct sp_pipebase, delayed_in);
            sockbase->vfptr->in (sockbase, (struct sp_pipe*) pipe);
        }
        sp_list_clear (&sockbase->delayed_ins);
        sp_mutex_unlock (&sockbase->delayed_sync);

        /*  Try to receive the message in a non-blocking way. */
        rc = sockbase->vfptr->recv (sockbase, buf, len);
        sp_mutex_unlock (&sockbase->sync);
        if (sp_fast (rc == 0))
            return 0;

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN))
            return rc;

        /*  If the message cannot be received at the moment and the recv call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT))
            return -EAGAIN;

        /*  With blocking recv, wait while there are new pipes available
            for receiving. */
        sp_mutex_lock (&sockbase->delayed_sync);
        if (sp_list_empty (&sockbase->delayed_ins)) {
            rc = sp_cond_wait (&sockbase->delayed_cond,
                &sockbase->delayed_sync, -1);
            errnum_assert (rc == 0, rc);
        }

        sp_mutex_lock (&sockbase->sync);
    }  
}

int sp_sock_fd (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    return sockbase->fd;
}

int sp_sock_add (struct sp_sock *self, struct sp_pipe *pipe)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);
    rc = sockbase->vfptr->add (sockbase, pipe);
    sp_mutex_unlock (&sockbase->sync);

    return rc;
}

void sp_sock_rm (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);
    sockbase->vfptr->rm (sockbase, pipe);
    sp_mutex_unlock (&sockbase->sync);
}

void sp_sock_in (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;
    struct sp_pipebase *pipebase;

    sockbase = (struct sp_sockbase*) self;
    pipebase = (struct sp_pipebase*) pipe;

    sp_mutex_lock (&sockbase->delayed_sync);
    sp_list_insert (&sockbase->delayed_ins, &pipebase->delayed_in,
        sp_list_end (&sockbase->delayed_ins));
    sp_cond_signal (&sockbase->delayed_cond);
    sp_mutex_unlock (&sockbase->delayed_sync);    
}

void sp_sock_out (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;
    struct sp_pipebase *pipebase;

    sockbase = (struct sp_sockbase*) self;
    pipebase = (struct sp_pipebase*) pipe;

    sp_mutex_lock (&sockbase->delayed_sync);
    sp_list_insert (&sockbase->delayed_outs, &pipebase->delayed_out,
        sp_list_end (&sockbase->delayed_outs));
    sp_cond_signal (&sockbase->delayed_cond);
    sp_mutex_unlock (&sockbase->delayed_sync);
}

void sp_timer_start (struct sp_timer *self, struct sp_sockbase *sockbase,
    int timeout, void (*fn) (struct sp_timer *self))
{
    struct sp_list_item *it;
    struct sp_timer *itt;

    sp_mutex_lock (&sockbase->sync);
    self->timeout = sp_clock_now (&sockbase->clock) + timeout;
    self->fn = fn;
    for (it = sp_list_begin (&sockbase->timers);
          it != sp_list_end (&sockbase->timers);
          it = sp_list_next (&sockbase->timers, it)) {
        itt = sp_cont (it, struct sp_timer, list);
        if (self->timeout < itt->timeout)
            break;
    }
    sp_list_insert (&sockbase->timers, &self->list, it);
    if (&self->list == sp_list_begin (&sockbase->timers))
        sp_cp_post (&sockbase->cp, &sockbase->timers_modified);
    sp_mutex_unlock (&sockbase->sync);
}

void sp_timer_cancel (struct sp_timer *self, struct sp_sockbase *sockbase)
{
    int signal;

    sp_mutex_lock (&sockbase->sync);
    signal = (&self->list == sp_list_begin (&sockbase->timers)) ? 1 : 0;
    sp_list_erase (&sockbase->timers, &self->list);
    if (signal)
        sp_cp_post (&sockbase->cp, &sockbase->timers_modified);
    sp_mutex_unlock (&sockbase->sync);
}

static void sp_sock_worker_routine (void *arg)
{
    int rc;
    int timeout;
    uint64_t now;
    struct sp_sockbase *sockbase;
    struct sp_timer *timer;
    struct sp_cp_task *task;

    sockbase = (struct sp_sockbase*) arg;

    /*  Get the current time. */
    now = sp_clock_now (&sockbase->clock);

    sp_mutex_lock (&sockbase->sync);

    while (1) {

        /*  Compute the waiting period till first timer expires. */
        if (sp_list_empty (&sockbase->timers))
            timeout = -1;
        else {
            timer = sp_cont (sp_list_begin (&sockbase->timers),
                struct sp_timer, list);
            timeout = (int) (timer->timeout - now);
            if (timeout < 0)
                timeout = 0;
        }

        /*  Wait for a completion of an operation or a timer expiration. */
        sp_mutex_unlock (&sockbase->sync);
        rc = sp_cp_wait (&sockbase->cp, timeout, &task);
        errnum_assert (rc == 0 || rc == -ETIMEDOUT, -rc);
        sp_mutex_lock (&sockbase->sync);

        /*  If there's a task completion event available. */
        if (rc == 0) {

            /*  If the socket is terminating, exit the worker thread. */
            if (task != &sockbase->stop)
                break;

            /*  If timers were modified do nothing and move straight to
                execution and re-computation of the timers. */
            if (task != &sockbase->timers_modified) {
                sp_assert (0);
            }
        }

        /*  Adjust the current time. */
        now = sp_clock_now (&sockbase->clock);

        /* Execute the timers. */
        while (1) {
            if (sp_list_empty (&sockbase->timers))
                break;
            timer = sp_cont (sp_list_begin (&sockbase->timers),
                struct sp_timer, list);
            if (timer->timeout > now)
                break;
            sp_list_erase (&sockbase->timers, &timer->list);

            /*  Invoke the timer callback. The important point here is that
                the timer structure is not referenced anymore and none of its
                members will be used again. Thus, callback is free to re-use
                it to launch a new timer. */
            timer->fn (timer);
        }
    }
}

