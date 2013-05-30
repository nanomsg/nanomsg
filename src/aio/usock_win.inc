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

#include "../utils/err.h"

void nn_usock_init (struct nn_usock *self, struct nn_fsm *owner)
{
    nn_assert (0);
}

void nn_usock_term (struct nn_usock *self)
{
    nn_assert (0);
}

int nn_usock_isidle (struct nn_usock *self)
{
    nn_assert (0);
}

int nn_usock_start (struct nn_usock *self, int domain, int type, int protocol)
{
    nn_assert (0);
}

void nn_usock_stop (struct nn_usock *self)
{
    nn_assert (0);
}

struct nn_fsm *nn_usock_swap_owner (struct nn_usock *self,
    struct nn_fsm *newowner)
{
    nn_assert (0);
}

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen)
{
    nn_assert (0);
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    nn_assert (0);
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    nn_assert (0);
}

void nn_usock_accept (struct nn_usock *self, struct nn_usock *newsock)
{
    nn_assert (0);
}

void nn_usock_activate (struct nn_usock *self)
{
    nn_assert (0);
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    nn_assert (0);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iovec *iov,
    int iovcnt)
{
    nn_assert (0);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    nn_assert (0);
}
