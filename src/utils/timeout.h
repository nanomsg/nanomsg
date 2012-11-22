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

#ifndef SP_TIMEOUT_INCLUDED
#define SP_TIMEOUT_INCLUDED

#include "clock.h"
#include "list.h"

/*  This class stores a list of timeouts and reports the next one to expire
    along with the time till it happens. */

struct sp_timeout_hndl {
    struct sp_list_item list;
    uint64_t timeout;
};

struct sp_timeout {
    struct sp_clock clock;
    struct sp_list timeouts;
};

void sp_timeout_init (struct sp_timeout *self);
void sp_timeout_term (struct sp_timeout *self);
int sp_timeout_add (struct sp_timeout *self, int timeout,
    struct sp_timeout_hndl *hndl);
int sp_timeout_rm (struct sp_timeout *self, struct sp_timeout_hndl *hndl);
int sp_timeout_timeout (struct sp_timeout *self);
int sp_timeout_event (struct sp_timeout *self, struct sp_timeout_hndl **hndl);

#endif

