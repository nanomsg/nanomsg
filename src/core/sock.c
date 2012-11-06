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

#define SP_SOCK_OP_STOP 1
#define SP_SOCK_OP_TIMERS 2
#define SP_SOCK_OP_IN 3
#define SP_SOCK_OP_OUT 4

/*  Private functions. */
void sp_sockbase_timer_routine (struct sp_timer *self);
static void sp_sock_worker_routine (void *arg);

void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    self->vfptr = vfptr;
    sp_mutex_init (&self->sync, 0);
    sp_cond_init (&self->cond);
    self->fd = fd;
    sp_clock_init (&self->clock);
    sp_list_init (&self->timers);
    sp_cp_init (&self->cp);
    sp_thread_init (&self->worker, sp_sock_worker_routine, self);
}

struct sp_cp *sp_sockbase_getcp (struct sp_sockbase *self)
{
    return &self->cp;
}

void sp_sock_term (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    /*  Terminate the derived class. */
    sockbase->vfptr->term (sockbase);

    /*  Ask the worker thread to terminate and wait till it does. */
    sp_cp_post (&sockbase->cp, SP_SOCK_OP_STOP, NULL);
    sp_thread_term (&sockbase->worker);
    sp_cp_term (&sockbase->cp);
    sp_list_term (&sockbase->timers);
    sp_clock_term (&sockbase->clock);

    /*  Terminate the sp_sockbase itself. */
    sp_cond_term (&sockbase->cond);
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

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);

    while (1) {

        /*  Try to send the message in a non-blocking way. */
        rc = sockbase->vfptr->send (sockbase, buf, len);
        if (sp_fast (rc == 0)) {
            sp_mutex_unlock (&sockbase->sync);
            return 0;
        }

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN)) {
            sp_mutex_unlock (&sockbase->sync);
            return rc;
        }

        /*  If the message cannot be sent at the moment and the send call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT)) {
            sp_mutex_unlock (&sockbase->sync);
            return -EAGAIN;
        }

        /*  With blocking send, wait while there are new pipes available
            for sending. */
        rc = sp_cond_wait (&sockbase->cond, &sockbase->sync, -1);
        errnum_assert (rc == 0, rc);
    }   
}

int sp_sock_recv (struct sp_sock *self, void *buf, size_t *len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_mutex_lock (&sockbase->sync);

    while (1) {

        /*  Try to receive the message in a non-blocking way. */
        rc = sockbase->vfptr->recv (sockbase, buf, len);
        if (sp_fast (rc == 0)) {
            sp_mutex_unlock (&sockbase->sync);
            return 0;
        }

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN)) {
            sp_mutex_unlock (&sockbase->sync);
            return rc;
        }

        /*  If the message cannot be received at the moment and the recv call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT)) {
            sp_mutex_unlock (&sockbase->sync);
            return -EAGAIN;
        }

        /*  With blocking recv, wait while there are new pipes available
            for receiving. */
        rc = sp_cond_wait (&sockbase->cond, &sockbase->sync, -1);
        errnum_assert (rc == 0, rc);
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
    sp_cp_post (&((struct sp_sockbase*) self)->cp, SP_SOCK_OP_IN, pipe);
}

void sp_sock_out (struct sp_sock *self, struct sp_pipe *pipe)
{
    sp_cp_post (&((struct sp_sockbase*) self)->cp, SP_SOCK_OP_OUT, pipe);
}

void sp_timer_start (struct sp_timer *self, struct sp_sockbase *sockbase,
    int timeout, void (*fn) (struct sp_timer *self))
{
    struct sp_list_item *it;
    struct sp_timer *itt;

    /*  No need to lock the socket mutex here as the function is always
        called from within the socket. I.e. the mutex is already locked. */
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
        sp_cp_post (&sockbase->cp, SP_SOCK_OP_TIMERS, NULL);
}

void sp_timer_cancel (struct sp_timer *self, struct sp_sockbase *sockbase)
{
    int signal;

    /*  No need to lock the socket mutex here as the function is always
        called from within the socket. I.e. the mutex is already locked. */
    signal = (&self->list == sp_list_begin (&sockbase->timers)) ? 1 : 0;
    sp_list_erase (&sockbase->timers, &self->list);
    if (signal)
        sp_cp_post (&sockbase->cp, SP_SOCK_OP_TIMERS, NULL);
}

static void sp_sock_worker_routine (void *arg)
{
    int rc;
    int timeout;
    uint64_t now;
    struct sp_sockbase *sockbase;
    struct sp_timer *timer;
    int op;
    void *oparg;

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
        rc = sp_cp_wait (&sockbase->cp, timeout, &op, &oparg);
        errnum_assert (rc == 0 || rc == -ETIMEDOUT, -rc);
        sp_mutex_lock (&sockbase->sync);

        /*  If there's a task completion event available. */
        if (rc == 0) {

            /*  Handle inbound pipes. */
            if (sp_fast (op == SP_SOCK_OP_IN)) {
                rc = sockbase->vfptr->in (sockbase, (struct sp_pipe*) oparg);
                errnum_assert (rc >= 0, -rc);
                if (rc > 0)
                    sp_cond_post (&sockbase->cond);
            }

            /*  Handle outbound pipes. */
            else if (sp_fast (op == SP_SOCK_OP_OUT)) {
                rc = sockbase->vfptr->out (sockbase, (struct sp_pipe*) oparg);
                errnum_assert (rc >= 0, -rc);
                if (rc > 0)
                    sp_cond_post (&sockbase->cond);
            }

            /*  If timers were modified do nothing and move straight to
                execution and re-computation of the timers. */
            else if (sp_fast (op == SP_SOCK_OP_TIMERS)) {
            }

            /*  If the socket is terminating, exit the worker thread. */
            else if (sp_fast (op == SP_SOCK_OP_STOP))
                break;

            /*  Unknown operation. */
            else
                sp_assert (0);
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

    sp_mutex_unlock (&sockbase->sync);
}

