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

#include "cond.h"
#include "fast.h"
#include "aio.h"
#include "err.h"

#ifdef SP_HAVE_WINDOWS

void sp_cond_init (struct sp_cond *self)
{
    InitializeConditionVariable (&self->cond);
    self->infinite = 1;
    sp_clock_init (&self->clock);
}

void sp_cond_term (struct sp_cond *self)
{
    sp_clock_term (&self->clock);

    /*  It seems that on Windows there's no need to terminate
        the condition variable. */
}

void sp_cond_set_timeout (struct sp_cond *self, int timeout)
{
    /*  Infinite timeout. */
    if (timeout < 0) {
        self->infinite = 1;
        return;
    }

    /*  Finite timeout. */
    self->infinite = 0;
    self->timeout = sp_clock_now (&self->clock) + timeout;
}

int sp_cond_wait (struct sp_cond *self, struct sp_mutex *mutex)
{
    uint64_t timeout;
    BOOL brc;

    if (self->infinite)
        timeout = INFINITE;
    else {
        timeout = sp_clock_now (&self->clock);
        if (timeout >= self->timeout)
            timeout = 0;
        else
            timeout = self->timeout - timeout;
    }

    brc = SleepConditionVariableCS (&self->cond, &mutex->mutex,
        (DWORD) timeout);
    if (sp_slow (!brc)) {
        if (GetLastError () == ERROR_TIMEOUT)
            return -ETIMEDOUT;
        win_assert (0);
    }

    return 0;
}

void sp_cond_post (struct sp_cond *self)
{
    WakeConditionVariable (&self->cond);
}

#else

#include <stdint.h>

void sp_cond_init (struct sp_cond *self)
{
    int rc;
    pthread_condattr_t attr;

    rc = pthread_condattr_init (&attr);
    errnum_assert (rc == 0, rc);
#if defined SP_HAVE_OSX
#elif defined SP_HAVE_CLOCK_MONOTONIC
    rc = pthread_condattr_setclock (&attr, CLOCK_MONOTONIC);
    errnum_assert (rc == 0, rc);
#else
    rc = pthread_condattr_setclock (&attr, CLOCK_REALTIME);
    errnum_assert (rc == 0, rc);
#endif
    rc = pthread_cond_init (&self->cond, &attr);
    errnum_assert (rc == 0, rc);
    rc = pthread_condattr_destroy (&attr);
    errnum_assert (rc == 0, rc);
    self->infinite = 1;
}

void sp_cond_term (struct sp_cond *self)
{
    int rc;

    rc = pthread_cond_destroy (&self->cond);
    errnum_assert (rc == 0, rc);
}

void sp_cond_set_timeout (struct sp_cond *self, int timeout)
{
    int rc;
#if defined SP_HAVE_OSX
    struct timeval tv;
#endif

    /*  Infinite timeout. */
    if (timeout < 0) {
        self->infinite = 1;
        return;
    }

    /*  Finite timeout. Get current time and add the specified interval. */
    self->infinite = 0;

    /*  On OSX the gettimeofday-based clock is hard-wired to the condition
        variable. There's no way to use different clock. */
#if defined SP_HAVE_OSX
    gettimeofday(&tv, NULL);
    TIMEVAL_TO_TIMESPEC(&tv, &self->timeout);

    /*  On platforms that provide monotonic clock, use it. That way we avoid
        problems when system time is adjusted. */
#elif defined SP_HAVE_CLOCK_MONOTONIC
    rc = clock_gettime (CLOCK_MONOTONIC, &self->timeout);
    errno_assert (rc == 0);

    /*  As a last resort use CLOCK_REALTIME. POSIX requires that this realtime
        clock should be available on any system. */
#else
    rc = clock_gettime (CLOCK_REALTIME, &self->timeout);
    errno_assert (rc == 0);
#endif
    self->timeout.tv_sec += timeout / 1000;
    self->timeout.tv_nsec += (timeout % 1000) * 1000000;
    if (self->timeout.tv_nsec >= 1000000000) {
        ++self->timeout.tv_sec;
        self->timeout.tv_nsec -= 1000000000;
    }
}

int sp_cond_wait (struct sp_cond *self, struct sp_mutex *mutex)
{
    int rc;
    struct timespec ts;

    if (self->infinite) {
        rc = pthread_cond_wait (&self->cond, &mutex->mutex);
        errnum_assert (rc == 0, rc);
    }
    else {
        rc = pthread_cond_timedwait (&self->cond, &mutex->mutex,
            &self->timeout);
        if (rc == ETIMEDOUT)
            return -ETIMEDOUT;
        errnum_assert (rc == 0, rc);
    }

    return 0;
}

void sp_cond_post (struct sp_cond *self)
{
    int rc;

    rc = pthread_cond_signal (&self->cond);
    errnum_assert (rc == 0, rc);
}

#endif

