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

#include "ctx.h"

#include "../utils/err.h"
#include "../utils/cont.h"

/*  Private functions. */
static void nn_worker_routine (void *arg);

void nn_worker_task_init (struct nn_worker_task *self,struct nn_fsm *owner)
{
    nn_assert (0);
}

void nn_worker_task_term (struct nn_worker_task *self)
{
    nn_assert (0);
}


int nn_worker_init (struct nn_worker *self)
{
    self->hndl = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self->hndl);
    nn_timerset_init (&self->timerset);
    nn_thread_init (&self->thread, nn_worker_routine, self);

    return 0;
}

void nn_worker_term (struct nn_worker *self)
{
    BOOL brc;

    nn_thread_term (&self->thread);
    nn_timerset_term (&self->timerset);
    brc = CloseHandle (self->hndl);
    win_assert (brc);
}

void nn_worker_execute (struct nn_worker *self, struct nn_worker_task *task)
{
    nn_assert (0);
}

void nn_worker_add_timer (struct nn_worker *self, int timeout,
    struct nn_worker_timer *timer)
{
    nn_timerset_add (&((struct nn_worker*) self)->timerset, timeout,
        &timer->hndl);
}

void nn_worker_rm_timer (struct nn_worker *self, struct nn_worker_timer *timer)
{
    nn_timerset_rm (&((struct nn_worker*) self)->timerset, &timer->hndl);
}

static void nn_worker_routine (void *arg)
{
    int rc;
    BOOL brc;
    struct nn_worker *self;
    int timeout;

    DWORD nbytes;
    ULONG_PTR key;
    LPOVERLAPPED olpd;
    struct nn_timerset_hndl *thndl;
    struct nn_worker_timer *timer;

#if 0
    struct nn_event *event;
    struct nn_usock_op *op;
    struct nn_usock *usock;
#endif

    self = (struct nn_worker*) arg;

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = nn_timerset_timeout (&self->timerset);

        /*  Wait for new events and/or timeouts. */
        /*  TODO: In theory we may gain some performance by getting multiple
            events at once via GetQueuedCompletionStatusEx function. */
        brc = GetQueuedCompletionStatus (self->hndl, &nbytes, &key,
            &olpd, timeout < 0 ? INFINITE : timeout);
        win_assert (brc || !olpd);

        /*  Process all expired timers. */
        while (1) {
            rc = nn_timerset_event (&self->timerset, &thndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);
            timer = nn_cont (thndl, struct nn_worker_timer, hndl);
            nn_ctx_enter (timer->owner->ctx);
            timer->owner->fn (timer->owner, timer, NN_WORKER_TIMER_TIMEOUT);
            nn_ctx_leave (timer->owner->ctx);
        }





#if 0
        /*  Timer event requires no processing. Its sole intent is to
            interrupt the polling in the worker thread. */
        if (nn_slow ((char*) key == &self->timer_event))
            continue;

        /*  Completion port shutdown is underway. Exit the worker thread. */
        if (nn_slow ((char*) key == &self->stop_event))
            break;

        /*  Custom events are reported via callback. */
        if (key) {
            event = (struct nn_event*) key;
            nn_assert ((*event->sink)->event);
            (*event->sink)->event (event->sink, event);
            event->active = 0;
            continue;
        }
        
        /*  I/O completion events */
        nn_assert (olpd);
        op = nn_cont (olpd, struct nn_usock_op, olpd);
        switch (op->op) {
        case NN_USOCK_OP_RECV:
            usock = nn_cont (op, struct nn_usock, in);
            nn_assert ((*usock->sink)->received);
printf ("received olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->received (usock->sink, usock);
            break;
        case NN_USOCK_OP_SEND:
            usock = nn_cont (op, struct nn_usock, out);
            nn_assert ((*usock->sink)->sent);
printf ("sent olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->sent (usock->sink, usock);
            break;
        case NN_USOCK_OP_CONNECT:
            usock = nn_cont (op, struct nn_usock, out);
            nn_assert ((*usock->sink)->connected);
printf ("connected olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->connected (usock->sink, usock);
            break;
        case NN_USOCK_OP_ACCEPT:
            usock = nn_cont (op, struct nn_usock, in);
            nn_assert ((*usock->sink)->accepted);
printf ("accepted olpd=%p\n", (void*) &op->olpd);
            (*usock->sink)->accepted (usock->sink, usock, usock->newsock);
            break;
        case NN_USOCK_OP_CONN:
            usock = nn_cont (op, struct nn_usock, conn);
            nn_assert (0);
        default:
            nn_assert (0);
        }
#endif
    }
}
