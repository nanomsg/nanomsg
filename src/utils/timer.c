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

#include "timer.h"
#include "mutex.h"
#include "clock.h"
#include "list.h"
#include "cont.h"
#include "cond.h"
#include "thread.h"
#include "err.h"

struct sp_timer_ctx {
    struct sp_mutex sync;
    struct sp_clock clock;
    struct sp_list timers;
    struct sp_cond cond;
    struct sp_thread worker;
    int stop;
};

/*  Singleton instance of sp_timer_ctx. */
static struct sp_timer_ctx self;

/*  Private functions. */
static void sp_timer_ctx_worker (void *arg);

void sp_timer_init (void)
{
    sp_mutex_init (&self.sync, SP_MUTEX_RECURSIVE);
    sp_clock_init (&self.clock);
    sp_list_init (&self.timers);
    sp_cond_init (&self.cond);
    self.stop = 0;
    sp_thread_init (&self.worker, sp_timer_ctx_worker, NULL);
}

void sp_timer_term (void)
{
    /*  Ask the worker thread to terminate. */
    sp_mutex_lock (&self.sync);
    self.stop = 1;
    sp_cond_signal (&self.cond);
    sp_mutex_unlock (&self.sync);

    /*  Wait while worker thread terminates. */
    sp_thread_term (&self.worker);

    sp_cond_term (&self.cond);
    sp_list_term (&self.timers);
    sp_clock_term (&self.clock);
    sp_mutex_term (&self.sync);
}

void sp_timer_start (struct sp_timer *timer, int timeout,
    void (*fn) (struct sp_timer *self))
{
    struct sp_list_item *it;
    struct sp_timer *itt;

    sp_mutex_lock (&self.sync);
    timer->timeout = sp_clock_now (&self.clock) + timeout;
    timer->fn = fn;
    for (it = sp_list_begin (&self.timers); it != sp_list_end (&self.timers);
          it = sp_list_next (&self.timers, it)) {
        itt = sp_cont (it, struct sp_timer, list);
        if (timer->timeout < itt->timeout)
            break;
    }
    sp_list_insert (&self.timers, &timer->list, it);
    if (&timer->list == sp_list_begin (&self.timers))
        sp_cond_signal (&self.cond);
    sp_mutex_unlock (&self.sync);
}

void sp_timer_cancel (struct sp_timer *timer)
{
    int signal;

    sp_mutex_lock (&self.sync);
    signal = (&timer->list == sp_list_begin (&self.timers)) ? 1 : 0;
    sp_list_erase (&self.timers, &timer->list);
    if (signal)
        sp_cond_signal (&self.cond);
    sp_mutex_unlock (&self.sync);
}

static void sp_timer_ctx_worker (void *arg)
{
    int rc;
    int timeout;
    uint64_t now;
    struct sp_timer *timer;

    sp_mutex_lock (&self.sync);

    while (1) {

        /*  The timer subsystem is being terminated. */
        if (sp_slow (self.stop == 1))
            break;

        /*  Compute the waiting period till first timer expires. */
        if (sp_list_empty (&self.timers))
            timeout = -1;
        else {
            now = sp_clock_now (&self.clock);
            timer = sp_cont (sp_list_begin (&self.timers),
                struct sp_timer, list);
            timeout = (int) (timer->timeout - now);
            if (timeout < 0)
                timeout = 0;
        }

        /*  During this call the mutex is unlocked. Thus, timers may be added
            or removed. */
        rc = sp_cond_wait (&self.cond, &self.sync, timeout);
        errnum_assert (rc == 0 || rc == -ETIMEDOUT, -rc);

        /*  Handle spurious wake-up. */
        if (sp_slow (rc == 0))
            continue;

        /* Execute the timers. */
        now = sp_clock_now (&self.clock);
        while (1) {
            if (sp_list_empty (&self.timers))
                break;
            timer = sp_cont (sp_list_begin (&self.timers),
                struct sp_timer, list);
            if (timer->timeout > now)
                break;
            sp_list_erase (&self.timers, &timer->list);

            /*  Invoke the timer callback. The important point here is that
                the timer structure is not referenced anymore and none of its
                members will be used again. Thus, callback is free to re-use
                it to launch a new timer. */
            timer->fn (timer);
        }
    }

    sp_mutex_unlock (&self.sync);
}

