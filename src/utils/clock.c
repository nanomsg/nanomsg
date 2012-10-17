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

#ifdef SP_HAVE_WINDOWS
#include "win.h"
#else
#include <time.h>
#endif

#include "clock.h"
#include "fast.h"
#include "err.h"

/* 1 millisecond expressed in CPU ticks. The value is chosen is such a way that
   it works pretty well for CPU frequencies above 500MHz. */
#define SP_CLOCK_PRECISION 1000000

static uint64_t sp_clock_rdtsc ()
{
#if (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
    return __rdtsc ();
#elif (defined __GNUC__ && (defined __i386__ || defined __x86_64__))
    uint32_t low;
    uint32_t high;
    __asm__ volatile ("rdtsc" : "=a" (low), "=d" (high));
    return (uint64_t) high << 32 | low;
#elif (defined __SUNPRO_CC && (__SUNPRO_CC >= 0x5100) && (defined __i386 || \
    defined __amd64 || defined __x86_64))
    union {
        uint64_t u64val;
        uint32_t u32val [2];
    } tsc;
    asm("rdtsc" : "=a" (tsc.u32val [0]), "=d" (tsc.u32val [1]));
    return tsc.u64val;
#else

    /*  RDTSC is not available. */
    return 0;
#endif
}

static uint64_t sp_clock_time ()
{
#ifdef SP_HAVE_WINDOWS

    LARGE_INTEGER tps;
    LARGE_INTEGER time;
    double tpms;

    QueryPerformanceFrequency (&tps);
    QueryPerformanceCounter (&time);
    tpms = (double) (tps.QuadPart / 1000);     
    return (uint64_t) (time.QuadPart / tpms);

#else

    int rc;
    struct timespec tv;

    rc = clock_gettime (CLOCK_MONOTONIC, &tv);
    errno_assert (rc == 0);
    return tv.tv_sec * (uint64_t) 1000 + tv.tv_nsec / 1000000;

#endif
}

void sp_clock_init (struct sp_clock *self)
{
    self->last_tsc = sp_clock_rdtsc ();
    self->last_time = sp_clock_time ();
}

void sp_clock_term (struct sp_clock *self)
{
}

uint64_t sp_clock_now (struct sp_clock *self)
{
    /*  If TSC is not supported, use the non-optimised time measurement. */
    uint64_t tsc = sp_clock_rdtsc ();
    if (!tsc)
        return sp_clock_time ();

    /*  If tsc haven't jumped back or run away too far, we can use the cached
        time value. */
    if (sp_fast (tsc - self->last_tsc <= (SP_CLOCK_PRECISION / 2) &&
          tsc >= self->last_tsc))
        return self->last_time;

    /*  It's a long time sice we've last measured the time. We'll do a new
        measurement now. */
    self->last_tsc = tsc;
    self->last_time = sp_clock_time ();
    return self->last_time;
}

uint64_t sp_clock_timestamp ()
{
    return sp_clock_rdtsc ();
}

