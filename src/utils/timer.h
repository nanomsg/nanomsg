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

#ifndef SP_TIMER_INCLUDED
#define SP_TIMER_INCLUDED

#include "list.h"

#include <stdint.h>

/*  Starts the timer subsystem. */
void sp_timer_init (void);

/*  Terminates the timer subsystem. */
void sp_timer_term (void);

struct sp_timer {
    struct sp_list_item list;
    uint64_t timeout;
    void (*fn) (struct sp_timer *self);
};

/*  Starts the timer. Timeout is specified in milliseconds. When the timer
    expires, 'fn' callback function is invoked. Timer object is terminated by
    then and can be re-used. */
void sp_timer_start (struct sp_timer *self, int timeout,
    void (*fn) (struct sp_timer *self));

/*  Cancel the timer that is still active. Never cancel a timer that has
    already expired. After this call, timer object is deinitialised and
    may be re-used. */
void sp_timer_cancel (struct sp_timer *self);

#endif

