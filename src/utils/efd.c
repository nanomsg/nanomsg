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

#include "efd.h"

#if !defined SP_HAVE_WINDOWS

#if defined SP_USE_SOCKETPAIR

#include "err.h"
#include "fast.h"

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

void sp_efd_init (struct sp_efd *self)
{
    int rc;
    int flags;
    int sp [2];

#if defined SOCK_CLOEXEC
    rc = socketpair (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sp);
#else
    rc = socketpair (AF_UNIX, SOCK_STREAM, 0, sp);
#endif
    errno_assert (rc == 0);
    self->r = sp [0];
    self->w = sp [1];

#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    rc = fcntl (self->r, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
    rc = fcntl (self->w, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    flags = fcntl (self->r, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	rc = fcntl (self->r, F_SETFL, flags | O_NONBLOCK);
    errno_assert (rc != -1);
}

void sp_efd_term (struct sp_efd *self)
{
    int rc;

    rc = close (self->r);
    errno_assert (rc == 0);
    rc = close (self->w);
    errno_assert (rc == 0);
}

int sp_efd_getfd (struct sp_efd *self)
{
    return self->r;
}

void sp_efd_signal (struct sp_efd *self)
{
    ssize_t nbytes;
    char c = 101;

    nbytes = send (self->w, &c, 1, 0);
    errno_assert (nbytes != -1);
    sp_assert (nbytes == 1);
}

void sp_efd_unsignal (struct sp_efd *self)
{
    ssize_t nbytes;
    uint8_t buf [16];

    while (1) {
        nbytes = recv (self->r, buf, sizeof (buf), 0);
        errno_assert (nbytes >= 0);
        if (sp_fast (nbytes < sizeof (buf)))
            break;
    }
}


#elif defined SP_USE_EVENTFD

#include "err.h"

#include <stdint.h>
#include <sys/eventfd.h>
#include <fcntl.h>

void sp_efd_init (struct sp_efd *self)
{
    int rc;
    int flags;

    self->efd = eventfd (0, EFD_CLOEXEC);
    errno_assert (self->efd != -1);

    flags = fcntl (self->efd, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	rc = fcntl (self->efd, F_SETFL, flags | O_NONBLOCK);
    errno_assert (rc != -1);
}

void sp_efd_term (struct sp_efd *self)
{
    int rc;

    rc = close (self->efd);
    errno_assert (rc == 0);
}

int sp_efd_getfd (struct sp_efd *self)
{
    return self->efd;
}

void sp_efd_signal (struct sp_efd *self)
{
    const uint64_t one = 1;
    ssize_t nbytes;

    nbytes = write (self->efd, &one, sizeof (one));
    errno_assert (nbytes == sizeof (one));
}

void sp_efd_unsignal (struct sp_efd *self)
{
    uint64_t count;

    /*  Extract all the signals from the eventfd. */
    ssize_t sz = read (self->efd, &count, sizeof (count));
    errno_assert (sz == sizeof (count));
    sp_assert (count > 0);
}

#endif

#endif

