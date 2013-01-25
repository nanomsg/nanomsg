/*
    Copyright (c) 2013 250bpm s.r.o.

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

#if NN_LATENCY_MONITOR

#include "latmon.h"
#include "clock.h"
#include "alloc.h"
#include "fast.h"
#include "err.h"

#include <stdint.h>
#include <stdio.h>

struct nn_latmon {
    uint64_t *latencies;
    int pos;
    int event;
} self;

void nn_latmon_init (void)
{
    self.latencies = nn_alloc (NN_LATENCY_MONITOR * NN_LATMON_EVENT_COUNT *
        sizeof (uint64_t), "latency table");
    alloc_assert (self.latencies);
    memset (self.latencies, 0, NN_LATENCY_MONITOR * NN_LATMON_EVENT_COUNT *
        sizeof (uint64_t));
    self.pos = 0;
    self.event = 0;
}

void nn_latmon_term (void)
{
    int rc;
    FILE *f;
    int i;
    int j;
    uint64_t min;

    f = fopen ("latency.log", "w+");
    errno_assert (f);
    min = self.latencies [0];
    for (i = 0; i != NN_LATENCY_MONITOR; ++i) {
        for (j = 0; j != NN_LATMON_EVENT_COUNT; ++j) {
            fprintf (f, "%llu", (long long unsigned int) self.latencies
                [i * NN_LATMON_EVENT_COUNT + j] - min);
            if (j != NN_LATMON_EVENT_COUNT - 1)
                fprintf (f, ",");
            else
                fprintf (f, "\n");
        }
    }
    rc = fclose (f);
    errno_assert (rc == 0);
    nn_free (self.latencies);
}

void nn_latmon_measure (int event)
{
    /*  Make sure that no event is skipped. */
    nn_assert (event == self.event);

    /*  Store the current time. */
    self.latencies [self.pos * NN_LATMON_EVENT_COUNT + event] =
        nn_clock_timestamp ();

    /*  Move to the next position in the latencies table. */
    ++self.event;
    if (nn_slow (self.event == NN_LATMON_EVENT_COUNT)) {
        self.event = 0;
        ++self.pos;
        if (nn_slow (self.pos == NN_LATENCY_MONITOR))
            self.pos = 0;
    }
}

#endif
