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
#include "err.h"

#ifdef SP_HAVE_WINDOWS

void sp_cond_init (struct sp_cond *self)
{
    InitializeConditionVariable (&self->cond);
}

void sp_cond_term (struct sp_cond *self)
{
    /*  It seems that on Windows there's no need to terminate
        the condition variable. */
}

int sp_cond_wait (struct sp_cond *self, struct sp_cp *cp, int timeout)
{
    BOOL brc;

    brc = SleepConditionVariableCS (&self->cond, &cp->sync.mutex,
        timeout < 0 ? INFINITE : timeout);
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

void sp_cond_init (struct sp_cond *self)
{
    int rc;

    rc = pthread_cond_init (&self->cond, NULL);
    errnum_assert (rc == 0, rc);
}

void sp_cond_term (struct sp_cond *self)
{
    int rc;

    rc = pthread_cond_destroy (&self->cond);
    errnum_assert (rc == 0, rc);
}

int sp_cond_wait (struct sp_cond *self, struct sp_cp *cp, int timeout)
{
    int rc;
    struct timespec ts;

    if (timeout < 0) {
        rc = pthread_cond_wait (&self->cond, &cp->sync.mutex);
        errnum_assert (rc == 0, rc);
    }
    else {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = timeout % 1000 * 1000000;
        rc = pthread_cond_timedwait (&self->cond, &cp->sync.mutex, &ts);
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

