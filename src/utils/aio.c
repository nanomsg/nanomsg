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

#include "aio.h"
#include "err.h"

void sp_aio_init (void)
{
   sp_assert (0);
}

void sp_aio_term (void)
{
   sp_assert (0);
}

void sp_aio_register (struct sp_usock *usock)
{
    sp_assert (0);
}

void sp_aio_send (struct sp_aio *self, struct sp_usock *usock,
    const void *buf, size_t *len, int flags)
{
   sp_assert (0);
}

void sp_aio_recv (struct sp_aio *self, struct sp_usock *usock,
    void *buf, size_t *len, int flags)
{
   sp_assert (0);
}

void sp_aio_wait (struct sp_aio **aio, int timeout)
{
   sp_assert (0);
}

