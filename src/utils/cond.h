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

#include "mutex.h"

/*  Platform-independent condition variable. */

#ifdef SP_HAVE_WINDOWS

#include "win.h"
#include "clock.h"

struct sp_cond {
    CONDITION_VARIABLE cond;
    int infinite;
    uint64_t timeout;
    struct sp_clock clock;
};

#else

#include <pthread.h>
#include <time.h>

struct sp_cond {
    pthread_cond_t cond;
    int infinite;
    struct timespec timeout;
};

#endif

/*  Initialises the condition variable. Timeout is set to -1 (infinite). */
void sp_cond_init (struct sp_cond *self);

/*  Destroys the condition variable. */
void sp_cond_term (struct sp_cond *self);

/*  Set timeout in milliseconds. The timeout interval starts at this moment.
    Multiple invocations sp_cond_wait can be done afterwards -- the timeout
    doesn't start anew with each sp_cond_wait call. Timeout of -1 means never
    time out. */
void sp_cond_set_timeout (struct sp_cond *self, int timeout);

/*  Wait for the condition. The mutex should be locked when this function is
    invoked. It will be locked on exit as well, except when -ETIMEDOUT is
    returned (timeout expired). */
int sp_cond_wait (struct sp_cond *self, struct sp_mutex *mutex);

/*  Signal the conditon variable. */
void sp_cond_post (struct sp_cond *self);

#endif

