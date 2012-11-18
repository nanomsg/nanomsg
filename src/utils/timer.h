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

#include "clock.h"
#include "list.h"

struct sp_timer_hndl {
    struct sp_list_item list;
    uint64_t timeout;
};

struct sp_timer {
    struct sp_clock clock;
    struct sp_list timers;
};

void sp_timer_init (struct sp_timer *self);
void sp_timer_term (struct sp_timer *self);
void sp_timer_add (struct sp_timer *self, int timeout,
    struct sp_timer_hndl *hndl);
void sp_timer_rm (struct sp_timer *self, struct sp_timer_hndl *hndl);
int sp_timer_timeout (struct sp_timer *self);
int sp_timer_event (struct sp_timer *self, struct sp_timer_hndl **hndl);

#endif

