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
#include "fast.h"
#include "cont.h"
#include "err.h"

void sp_timer_init (struct sp_timer *self)
{
    sp_clock_init (&self->clock);
    sp_list_init (&self->timers);
}

void sp_timer_term (struct sp_timer *self)
{
    sp_list_term (&self->timers);
    sp_clock_term (&self->clock);
}

int sp_timer_add (struct sp_timer *self, int timeout,
    struct sp_timer_hndl *hndl)
{
    struct sp_list_item *it;
    struct sp_timer_hndl *ith;
    int first;

    /*  Compute the instant when the timer will be due. */
    hndl->timeout = sp_clock_now (&self->clock) + timeout;

    /*  Insert it into the ordered list of timeouts. */
    for (it = sp_list_begin (&self->timers);
          it != sp_list_end (&self->timers);
          it = sp_list_next (&self->timers, it)) {
        ith = sp_cont (it, struct sp_timer_hndl, list);
        if (hndl->timeout < ith->timeout)
            break;
    }

    /*  If the new timer happens to be the first one to expire, let the user
        know that the current waiting interval has to be changed. */
    first = sp_list_begin (&self->timers) == it ? 1 : 0;
    sp_list_insert (&self->timers, &hndl->list, it);
    return first;
}

int sp_timer_rm (struct sp_timer *self, struct sp_timer_hndl *hndl)
{
    int first;

    /*  If it was the first timer that was removed, the actual waiting time
        may have changed. We'll thus return 1 to let the user know. */
    first = sp_list_begin (&self->timers) == &hndl->list ? 1 : 0;
    sp_list_erase (&self->timers, &hndl->list);
    return first;
}

int sp_timer_timeout (struct sp_timer *self)
{
    int timeout;

    if (sp_fast (sp_list_empty (&self->timers)))
        return -1;

    timeout = sp_cont (sp_list_begin (&self->timers),
        struct sp_timer_hndl, list)->timeout - sp_clock_now (&self->clock);
    return timeout < 0 ? 0 : timeout;
}

int sp_timer_event (struct sp_timer *self, struct sp_timer_hndl **hndl)
{
    struct sp_timer_hndl *first;

    /*  If there's no timer, there's no event to report. */
    if (sp_fast (sp_list_empty (&self->timers)))
        return -EAGAIN;

    /*  If no timer have expired yet, there's no event to return. */
    first = sp_cont (sp_list_begin (&self->timers), struct sp_timer_hndl, list);
    if (first->timeout > sp_clock_now (&self->clock))
        return -EAGAIN;

    /*  Return the first timer and remove it from the list of active timers. */
    sp_list_erase (&self->timers, &first->list);
    *hndl = first;
    return 0;
}

