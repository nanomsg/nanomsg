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

void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    self->vfptr = vfptr;
    sp_cmutex_init (&self->sync);
    self->fd = fd;
    sp_mutex_init (&self->delayed_sync, 0);
    sp_cond_init (&self->delayed_cond);
    sp_list_init (&self->delayed_ins);
    sp_list_init (&self->delayed_outs);
}

void sp_sockbase_timer_start (struct sp_sockbase *self,
    struct sp_sockbase_timer *timer, int timeout,
    void (*fn) (struct sp_sockbase_timer *self))
{
    sp_cmutex_start_cancelling (&self->sync);
    timer->sockbase = self;
    timer->fn = fn;
    sp_timer_start (&timer->timer, timeout, sp_sockbase_timer_routine);
    sp_cmutex_stop_cancelling (&self->sync);
}

void sp_sockbase_timer_cancel (struct sp_sockbase *self,
    struct sp_sockbase_timer *timer)
{
    sp_cmutex_start_cancelling (&self->sync);
    sp_timer_cancel (&timer->timer);
    sp_cmutex_stop_cancelling (&self->sync);
}

void sp_sockbase_timer_routine (struct sp_timer *self)
{
    int rc;
    struct sp_sockbase_timer *timer;

    timer = sp_cont (self, struct sp_sockbase_timer, timer);

    /*  Try to grab the ownership of the mutex. If it's in cancellation state
        drop the timer and don't invoke the callback. */
    rc = sp_cmutex_cancellable_lock (&timer->sockbase->sync);
    if (rc == -ECANCELED)
        return;
    errnum_assert (rc == 0, -rc);

    /*  Invoke the callback. */
    timer->fn (timer);

    sp_cmutex_unlock (&timer->sockbase->sync);
}

void sp_sock_term (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    /*  Terminate the derived class. */
    sockbase->vfptr->term (sockbase);

    /*  Terminate the sp_sockbase itself. */
    sp_list_term (&sockbase->delayed_outs);
    sp_list_term (&sockbase->delayed_ins);
    sp_cond_term (&sockbase->delayed_cond);
    sp_mutex_term (&sockbase->delayed_sync);
    sp_cmutex_term (&sockbase->sync);
}

int sp_sock_setopt (struct sp_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cmutex_lock (&sockbase->sync);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->setopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_cmutex_unlock (&sockbase->sync);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_cmutex_unlock (&sockbase->sync);
    return -ENOPROTOOPT;
}

int sp_sock_getopt (struct sp_sock *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cmutex_lock (&sockbase->sync);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->getopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_cmutex_unlock (&sockbase->sync);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_cmutex_unlock (&sockbase->sync);
    return -ENOPROTOOPT;
}

int sp_sock_send (struct sp_sock *self, const void *buf, size_t len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;
    struct sp_list_item *it;
    struct sp_pipebase *pipe;

    sockbase = (struct sp_sockbase*) self;

    sp_cmutex_lock (&sockbase->sync);
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
        sp_cmutex_unlock (&sockbase->sync);
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

        sp_cmutex_lock (&sockbase->sync);
    }   
}

int sp_sock_recv (struct sp_sock *self, void *buf, size_t *len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;
    struct sp_list_item *it;
    struct sp_pipebase *pipe;

    sockbase = (struct sp_sockbase*) self;

    sp_cmutex_lock (&sockbase->sync);
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
        sp_cmutex_unlock (&sockbase->sync);
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

        sp_cmutex_lock (&sockbase->sync);
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

    sp_cmutex_lock (&sockbase->sync);
    rc = sockbase->vfptr->add (sockbase, pipe);
    sp_cmutex_unlock (&sockbase->sync);

    return rc;
}

void sp_sock_rm (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cmutex_lock (&sockbase->sync);
    sockbase->vfptr->rm (sockbase, pipe);
    sp_cmutex_unlock (&sockbase->sync);
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

