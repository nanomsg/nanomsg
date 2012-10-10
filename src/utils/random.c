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

#include "config.h"

#ifdef SP_HAVE_WINDOWS
#include "win.h"
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#include <string.h>

#include "random.h"
#include "clock.h"
#include "fast.h"

static uint64_t sp_random_state = 0xfa9b23e307cc611f;

void sp_random_seed ()
{
    uint64_t pid;

#ifdef SP_HAVE_WINDOWS
    pid = (uint64_t) GetCurrentProcessId ();
#else
    pid = (uint64_t) getpid ();
#endif

    /*  The initial state for pseudo-random number generator is computer from
        the exact timestamp and process ID. */
    sp_random_state ^= pid + sp_clock_timestamp ();
}

void sp_random_generate (void *buf, size_t len)
{
    uint8_t *pos;

    pos = (uint8_t*) buf;

    while (1) {

        /*  Generate a pseudo-random integer. */
        sp_random_state = sp_random_state * 1103515245 + 12345;

        /*  Move the bytes to the output buffer. */
        memcpy (pos, &sp_random_state, len > 8 ? 8 : len);
        if (sp_fast (len <= 8))
            return;
        len -= 8;
        pos += 8;
    }
}

