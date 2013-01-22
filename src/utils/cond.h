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

#ifndef NN_COND_INCLUDED
#define NN_COND_INCLUDED

#include "mutex.h"

/*  Platform-independent condition variable. */

#ifdef NN_HAVE_WINDOWS

#include "win.h"
#include "clock.h"

struct nn_cond {
    CONDITION_VARIABLE cond;
    int infinite;
    uint64_t timeout;
    struct nn_clock clock;
};

#else

#include <pthread.h>
#include <time.h>

struct nn_cond {
    pthread_cond_t cond;
    int infinite;
    struct timespec timeout;
};

#endif

/*  Initialises the condition variable. Timeout is set to -1 (infinite). */
void nn_cond_init (struct nn_cond *self);

/*  Destroys the condition variable. */
void nn_cond_term (struct nn_cond *self);

/*  Set timeout in milliseconds. The timeout interval starts at this moment.
    Multiple invocations nn_cond_wait can be done afterwards -- the timeout
    doesn't start anew with each nn_cond_wait call. Timeout of -1 means never
    time out. */
void nn_cond_set_timeout (struct nn_cond *self, int timeout);

/*  Wait for the condition. The mutex should be locked when this function is
    invoked. It will be locked on exit as well, except when -ETIMEDOUT is
    returned (timeout expired). */
int nn_cond_wait (struct nn_cond *self, struct nn_mutex *mutex);

/*  Signal the conditon variable. */
void nn_cond_post (struct nn_cond *self);

#endif

