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

#include "signaler.h"

#ifndef SP_HAVE_WINDOWS

#if defined SP_USE_SOCKETPAIR

#include "err.h"

#include <sys/types.h>
#include <sys/socket.h>

void sp_signaler_init (struct sp_signaler *self)
{
    int rc;
    int sp [2];

    rc = socketpair (AF_UNIX, SOCK_STREAM, 0, sp);
    errno_assert (rc == 0);
    self->r = sp [0];
    self->w = sp [1];
}

void sp_signaler_term (struct sp_signaler *self)
{
    int rc;

    rc = close (self->r);
    errno_assert (rc == 0);
    rc = close (self->w);
    errno_assert (rc == 0);
}

int sp_signaler_fd (struct sp_signaler *self)
{
    return self->r;
}

void sp_signaler_post (struct sp_signaler *self)
{
    ssize_t nbytes;
    char c = 101;

    nbytes = send (self->w, &c, 1, 0);
    errno_assert (nbytes != -1);
    sp_assert (nbytes == 1);
}

#elif defined SP_USE_EVENTFD

#include "err.h"

#include <stdint.h>
#include <sys/eventfd.h>

void sp_signaler_init (struct sp_signaler *self)
{
    self->efd = eventfd (0, EFD_CLOEXEC);
    errno_assert (self->efd != -1);
}

void sp_signaler_term (struct sp_signaler *self)
{
    int rc;

    rc = close (self->efd);
    errno_assert (rc == 0);
}

int sp_signaler_fd (struct sp_signaler *self)
{
    return self->efd;
}

void sp_signaler_post (struct sp_signaler *self)
{
    const uint64_t one = 1;
    ssize_t nbytes;

    nbytes = write (self->efd, &one, sizeof (one));
    errno_assert (nbytes == sizeof (one));
}

#else
#error
#endif

#endif

