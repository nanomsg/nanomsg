/*
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#include "../src/nn.h"
#include "../src/reqrep.h"

#include "testutil.h"

#include <string.h>

int main ()
{
    int rc;
    int req;
    void *p;
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    /*  Try to create an oversized message. */
    p = nn_allocmsg (-1, 0);
    nn_assert (!p && nn_errno () == ENOMEM);
    p = nn_allocmsg (-1000, 0);
    nn_assert (!p && nn_errno () == ENOMEM);

    /*  Try to create a message of unknown type. */
    p = nn_allocmsg (100, 333);
    nn_assert (!p && nn_errno () == EINVAL);

    /*  Create a socket. */
    req = test_socket (AF_SP_RAW, NN_REQ);

    /*  Make send fail and check whether the zero-copy buffer is left alone
        rather than deallocated. */
    p = nn_allocmsg (100, 0);
    nn_assert (p);
    rc = nn_send (req, &p, NN_MSG, NN_DONTWAIT);
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EAGAIN);
    memset (p, 0, 100);
    rc = nn_freemsg (p);
    errno_assert (rc == 0);

    /*  Same thing with nn_sendmsg(). */
    p = nn_allocmsg (100, 0);
    nn_assert (p);
    iov.iov_base = &p;
    iov.iov_len = NN_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    nn_sendmsg (req, &hdr, NN_DONTWAIT);
    errno_assert (nn_errno () == EAGAIN);
    memset (p, 0, 100);
    rc = nn_freemsg (p);
    errno_assert (rc == 0);

    /*  Clean up. */
    test_close (req);

    return 0;
}

