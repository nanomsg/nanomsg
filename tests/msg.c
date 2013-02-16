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

#include "../src/nn.h"
#include "../src/pair.h"

#include "../src/utils/err.c"

#include <string.h>

#define SOCKET_ADDRESS "inproc://a"

int main ()
{
    int rc;
    int sb;
    int sc;
    unsigned char *buf1, *buf2;
    int i;
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    buf1 = nn_allocmsg (256, 0);
    alloc_assert (buf1);
    for (i = 0; i != 256; ++i)
        buf1 [i] = (unsigned char) i;
    rc = nn_send (sc, &buf1, NN_MSG, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);

    buf2 = NULL;
    rc = nn_recv (sb, &buf2, NN_MSG, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);
    nn_assert (buf2);
    for (i = 0; i != 256; ++i)
        nn_assert (buf2 [i] == (unsigned char) i);
    rc = nn_freemsg (buf2);
    errno_assert (rc == 0);

    buf1 = nn_allocmsg (256, 0);
    alloc_assert (buf1);
    for (i = 0; i != 256; ++i)
        buf1 [i] = (unsigned char) i;
    iov.iov_base = &buf1;
    iov.iov_len = NN_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    rc = nn_sendmsg (sc, &hdr, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);

    buf2 = NULL;
    iov.iov_base = &buf2;
    iov.iov_len = NN_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    rc = nn_recvmsg (sb, &hdr, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);
    nn_assert (buf2);
    for (i = 0; i != 256; ++i)
        nn_assert (buf2 [i] == (unsigned char) i);
    rc = nn_freemsg (buf2);
    errno_assert (rc == 0);

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

