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

#ifndef SP_COND_INCLUDED
#define SP_COND_INCLUDED

/*  Platform-independent condition variable. */

#include "mutex.h"
#include "clock.h"

#ifdef SP_HAVE_WINDOWS

#include "win.h"

struct sp_cond {
    CONDITION_VARIABLE cond;
    struct sp_clock clock;
};

#else

#include <pthread.h>

struct sp_cond {
    pthread_cond_t cond;
    struct sp_clock clock;
};

#endif

void sp_cond_init (struct sp_cond *self);
void sp_cond_term (struct sp_cond *self);

int sp_cond_wait (struct sp_cond *self,
    struct sp_mutex *mutex, int timeout);
void sp_cond_signal (struct sp_cond *self);

#endif

