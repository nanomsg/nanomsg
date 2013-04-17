/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "../utils/cont.h"
#include "../utils/err.h"

static void nn_timer_term (struct nn_timer *self);
static void nn_timer_callback_handler (struct nn_callback *self, void *source,
    int type);
static const struct nn_callback_vfptr nn_timer_vfptr =
    {nn_timer_callback_handler};

void nn_timer_init (struct nn_timer *self, struct nn_ctx *ctx,
    struct nn_callback *callback)
{
    nn_callback_init (&self->in_callback, &nn_timer_vfptr);
    self->out_callback = callback;
    nn_worker_task_init (&self->start_task, &self->in_callback);
    nn_worker_task_init (&self->stop_task, &self->in_callback);
    nn_worker_task_init (&self->close_task, &self->in_callback);
    nn_worker_timer_init (&self->wtimer, &self->in_callback);
    self->ctx = ctx;
    self->worker = nn_ctx_choose_worker (ctx);
    self->timeout = -1;
}

static void nn_timer_term (struct nn_timer *self)
{
    nn_worker_timer_term (&self->wtimer);
    nn_worker_task_term (&self->close_task);
    nn_worker_task_term (&self->stop_task);
    nn_worker_task_term (&self->start_task);
    nn_callback_term (&self->in_callback);
}

void nn_timer_close (struct nn_timer *self)
{
    /*  Ask timer to close asynchronously. */
    nn_worker_execute (self->worker, &self->close_task);
}

void nn_timer_start (struct nn_timer *self, int timeout)
{
    /*  Negative timeout make no sense. */
    nn_assert (timeout >= 0);

    /*  Make sure that the timer is not yet started. */
    nn_assert (self->timeout == -1);

    /*  Ask the worker thread to start waiting for the timeout. */
    self->timeout = timeout;
    nn_worker_execute (self->worker, &self->start_task);
}

void nn_timer_stop (struct nn_timer *self)
{
    /*  Ask the worker thread to stop the timer. */
    nn_worker_execute (self->worker, &self->start_task);
}

static void nn_timer_callback_handler (struct nn_callback *self, void *source,
    int type)
{
    struct nn_timer *timer;
    struct nn_callback *out_callback;

    timer = nn_cont (self, struct nn_timer, in_callback);

    if (source == &timer->wtimer) {
        nn_assert (timer->timeout > 0);
        timer->timeout = -1;
        nn_ctx_enter (timer->ctx);
        timer->out_callback->vfptr->callback (timer->out_callback,
            timer, NN_TIMER_TIMEOUT);
        nn_ctx_leave (timer->ctx);
        return;
    }
    if (source == &timer->start_task) {
        nn_assert (timer->timeout > 0);
        nn_worker_add_timer (timer->worker, timer->timeout, &timer->wtimer);
        return;
    }
    if (source == &timer->stop_task) {
        nn_assert (timer->timeout > 0);
        timer->timeout = -1;
        nn_worker_rm_timer (timer->worker, &timer->wtimer);
        nn_ctx_enter (timer->ctx);
        timer->out_callback->vfptr->callback (timer->out_callback,
            timer, NN_TIMER_STOPPED);
        nn_ctx_leave (timer->ctx);
        return;
    }
    if (source == &timer->close_task) {
        if (timer->timeout > 0)
            nn_worker_rm_timer (timer->worker, &timer->wtimer);
        out_callback = timer->out_callback;
        nn_timer_term (timer);
        nn_ctx_enter (timer->ctx);
        out_callback->vfptr->callback (out_callback, timer, NN_TIMER_CLOSED);
        nn_ctx_leave (timer->ctx);
        return;
    }
    nn_assert (0);
}

