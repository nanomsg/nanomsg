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

#include "glock.h"
#include "config.h"

#if defined SP_HAVE_WINDOWS

#include "win.h"
#include "err.h"

static INIT_ONCE sp_glock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION sp_glock_cs;

static BOOL CALLBACK sp_glock_init (PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    InitializeCriticalSection (&sp_glock_cs);
    return TRUE;
}

void sp_glock_lock (void)
{
    BOOL brc;

    brc = InitOnceExecuteOnce (&sp_glock_once, sp_glock_init, NULL, NULL);
    win_assert (brc);

    EnterCriticalSection (&sp_glock_cs);
}

void sp_glock_unlock (void)
{
    BOOL brc;

    brc = InitOnceExecuteOnce (&sp_glock_once, sp_glock_init, NULL, NULL);
    win_assert (brc);

    LeaveCriticalSection (&sp_glock_cs);
}

#else

#include "err.h"

#include <pthread.h>

static pthread_mutex_t sp_glock_mutex = PTHREAD_MUTEX_INITIALIZER;

void sp_glock_lock (void)
{
    int rc;

    rc = pthread_mutex_lock (&sp_glock_mutex);
    errnum_assert (rc == 0, rc);
}

void sp_glock_unlock (void)
{
    int rc;

    rc = pthread_mutex_unlock (&sp_glock_mutex);
    errnum_assert (rc == 0, rc);
}

#endif

