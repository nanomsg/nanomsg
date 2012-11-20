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

#if !defined SP_HAVE_WINDOWS

#include "aio.h"
#include "err.h"
#include "fast.h"

#include <sys/socket.h>

int sp_io_send (int s, const void *buf, size_t *len)
{
    ssize_t nbytes;

#if defined MSG_NOSIGNAL
    nbytes = send (s, buf, *len, MSG_NOSIGNAL);
#else
    nbytes = send (s, buf, *len, 0);
#endif

    /*  Success. */
    if (sp_fast (nbytes >= 0)) {
        *len = (size_t) nbytes;
        return 0;
    }

    /*  If the connection fails, return ECONNRESET. */
    sp_assert (errno == ECONNRESET || errno == ETIMEDOUT || errno == EPIPE);
    return -ECONNRESET;
}

int sp_io_recv (int s, void *buf, size_t *len)
{
    ssize_t nbytes;

    nbytes = recv (s, buf, *len, 0);

    /*  Success. */
    if (sp_fast (nbytes > 0)) {
        *len = (size_t) nbytes;
        return 0;
    }

    /*  If the peer closes the connection, return ECONNRESET. */
    sp_assert (nbytes == 0 || errno == ECONNRESET || errno == ENOTCONN ||
          errno == ECONNREFUSED || errno == ETIMEDOUT ||
          errno == EHOSTUNREACH);
    return -ECONNRESET;
}

#endif


