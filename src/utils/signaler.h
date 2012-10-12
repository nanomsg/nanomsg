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

#ifndef SP_SIGNALER_INCLUDED
#define SP_SIGNALER_INCLUDED

#include "config.h"

/*  This class alows to send a signal via a file descriptor. */

/*  There's no signaler on Windows. Implementing it would require a lot of
    black magic and introduce secutity holes (TCP connections over loopback)
    and thus we are rather going to assume that signaler won't be needed on
    Windows platform. */
#ifndef SP_HAVE_WINDOWS

struct sp_signaler;

void sp_signaler_init (struct sp_signaler *self);
void sp_signaler_term (struct sp_signaler *self);
int sp_signaler_fd (struct sp_signaler *self);
void sp_signaler_post (struct sp_signaler *self);

#if defined SP_USE_SOCKETPAIR

struct sp_signaler
{
    int r;
    int w;
};

#elif defined SP_USE_EVENTFD

struct sp_signaler
{
    int efd;
};

#else
#error
#endif

#endif

#endif
