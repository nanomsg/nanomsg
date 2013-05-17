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

/*  Timer state reflects the state as seen by the user thread. It says nothing
    about the state of affairs in the worker thread. */
#define NN_TIMER_STATE_IDLE 1
#define NN_TIMER_STATE_ACTIVE 2
#define NN_TIMER_STATE_STOPPING 3

#define NN_TIMER_EVENT_START 1
#define NN_TIMER_EVENT_STOP 2

/*  Private functions. */
static void nn_timer_handler (struct nn_fsm *self, void *source, int type);

void nn_timer_init (struct nn_timer *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_timer_handler, owner);
    self->state = NN_TIMER_STATE_IDLE;
    nn_worker_task_init (&self->start_task, &self->fsm);
    nn_worker_task_init (&self->stop_task, &self->fsm);
    nn_worker_timer_init (&self->wtimer, &self->fsm);
    nn_fsm_event_init (&self->timeout_event, self, NN_TIMER_TIMEOUT);
    nn_fsm_event_init (&self->stopped_event, self, NN_TIMER_STOPPED);
    self->worker = nn_fsm_choose_worker (&self->fsm);
    self->timeout = -1;
}

void nn_timer_term (struct nn_timer *self)
{
    nn_assert (self->state == NN_TIMER_STATE_IDLE);

    nn_fsm_event_term (&self->stopped_event);
    nn_fsm_event_term (&self->timeout_event);
    nn_worker_timer_term (&self->wtimer);
    nn_worker_task_term (&self->stop_task);
    nn_worker_task_term (&self->start_task);
    nn_fsm_term (&self->fsm);
}

int nn_timer_isidle (struct nn_timer *self)
{
    return self->state == NN_TIMER_STATE_IDLE ? 1 : 0;
}

int nn_timer_isstopped (struct nn_timer *self)
{
    return self->state == NN_TIMER_STATE_IDLE ||
        self->state == NN_TIMER_STATE_STOPPING ? 1 : 0;
}

void nn_timer_start (struct nn_timer *self, int timeout)
{
    /*  Negative timeout make no sense. */
    nn_assert (timeout >= 0);

    /*  Pass the event to the state machine. */
    self->timeout = timeout;
    nn_timer_handler (&self->fsm, NULL, NN_TIMER_EVENT_START);
}

void nn_timer_stop (struct nn_timer *self)
{
    /*  Pass the event to the state machine. */
    nn_assert (self->state != NN_TIMER_STATE_STOPPING);
    nn_timer_handler (&self->fsm, NULL, NN_TIMER_EVENT_STOP);
}

static void nn_timer_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_timer *timer;

    timer = nn_cont (self, struct nn_timer, fsm);

    /*  Internal tasks sent from the user thread to the worker thread. */
    if (source == &timer->start_task) {
        nn_assert (type == NN_WORKER_TASK_EXECUTE);
        nn_assert (timer->timeout >= 0);
        nn_worker_add_timer (timer->worker, timer->timeout,
            &timer->wtimer);
        timer->timeout = -1;
        return;
    }
    if (source == &timer->stop_task) {
        nn_assert (type == NN_WORKER_TASK_EXECUTE);        
        nn_worker_rm_timer (timer->worker, &timer->wtimer);
        timer->state = NN_TIMER_STATE_IDLE;
        nn_fsm_raise (&timer->fsm, &timer->stopped_event);
        return;
    }

    /*  The state machine itself. */
    switch (timer->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_TIMER_STATE_IDLE:
        if (source == NULL) {
            switch (type) {
            case NN_TIMER_EVENT_START:

                /*  Send start event to the worker thread. */
                nn_worker_execute (timer->worker, &timer->start_task);
                timer->state = NN_TIMER_STATE_ACTIVE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_TIMER_STATE_ACTIVE:
        if (source == &timer->wtimer) {
            switch (type) {
            case NN_WORKER_TIMER_TIMEOUT:

                /*  Notify the user about the timeout. */
                nn_assert (timer->timeout == -1);
                nn_fsm_raise (&timer->fsm, &timer->timeout_event);
                return;

            default:
                nn_assert (0);
            }
        }
        if (source == NULL) {
            switch (type) {
            case NN_TIMER_EVENT_STOP:

                /*  Send stop event to the worker thread. */
                nn_worker_execute (timer->worker, &timer->stop_task);
                timer->state = NN_TIMER_STATE_STOPPING;
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  STOPPING state.                                                           */
/******************************************************************************/
    case NN_TIMER_STATE_STOPPING:
        if (source == &timer->wtimer) {
            switch (type) {
            case NN_WORKER_TIMER_TIMEOUT:

                /*  Timer is being shut down anyway.
                    We can safely ignore the timeout. */
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

