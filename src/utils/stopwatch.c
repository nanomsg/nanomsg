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

#include "stopwatch.h"
#include "config.h"

#if defined SP_HAVE_WINDOWS

#include "win.h"

void sp_stopwatch_init (struct sp_stopwatch *self)
{
    LARGE_INTEGER time;

    QueryPerformanceCounter (&time);
    self->start = (uint64_t) (time.QuadPart);
}

uint64_t sp_stopwatch_term (struct sp_stopwatch *self)
{
    LARGE_INTEGER tps;
    LARGE_INTEGER time;
    double tpms;

    QueryPerformanceFrequency (&tps);
    tpms = (double) (tps.QuadPart / 1000);

    QueryPerformanceCounter (&time);
    return (uint64_t) ((time.QuadPart - self->start) / tpms);
}

#else

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

void sp_stopwatch_init (struct sp_stopwatch *self)
{
    int rc;
    struct timeval tv;

    rc = gettimeofday (&tv, NULL);
    assert (rc == 0);
    self->start = (uint64_t) (((uint64_t) tv.tv_sec) * 1000000 + tv.tv_usec);
}

uint64_t sp_stopwatch_term (struct sp_stopwatch *self)
{
    int rc;
    struct timeval tv;
    uint64_t end;

    rc = gettimeofday (&tv, NULL);
    assert (rc == 0);
    end = (uint64_t) (((uint64_t) tv.tv_sec) * 1000000 + tv.tv_usec);
    return end - self->start;
}

#endif
