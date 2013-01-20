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

#include "../src/sp.h"
#include "../src/pair.h"

#include "../src/utils/err.c"

#include <string.h>

int main ()
{
    int rc;
    int sb;
    int sc;
    unsigned char *buf1, *buf2;
    int i;
    struct sp_iovec iov;
    struct sp_msghdr hdr;

    rc = sp_init ();
    errno_assert (rc == 0);
    sb = sp_socket (AF_SP, SP_PAIR);
    errno_assert (sb != -1);
    rc = sp_bind (sb, "inproc://a");
    errno_assert (rc >= 0);
    sc = sp_socket (AF_SP, SP_PAIR);
    errno_assert (sc != -1);
    rc = sp_connect (sc, "inproc://a");
    errno_assert (rc >= 0);

    buf1 = sp_allocmsg (256, 0);
    alloc_assert (buf1);
    for (i = 0; i != 256; ++i)
        buf1 [i] = (unsigned char) i;
    rc = sp_send (sc, buf1, SP_MSG, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 256);

    buf2 = NULL;
    rc = sp_recv (sb, &buf2, SP_MSG, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 256);
    sp_assert (buf2);
    for (i = 0; i != 256; ++i)
        sp_assert (buf2 [i] == (unsigned char) i);
    rc = sp_freemsg (buf2);
    errno_assert (rc == 0);

    buf1 = sp_allocmsg (256, 0);
    alloc_assert (buf1);
    for (i = 0; i != 256; ++i)
        buf1 [i] = (unsigned char) i;
    iov.iov_base = buf1;
    iov.iov_len = SP_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    rc = sp_sendmsg (sc, &hdr, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 256);

    buf2 = NULL;
    iov.iov_base = &buf2;
    iov.iov_len = SP_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    rc = sp_recvmsg (sb, &hdr, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 256);
    sp_assert (buf2);
    for (i = 0; i != 256; ++i)
        sp_assert (buf2 [i] == (unsigned char) i);
    rc = sp_freemsg (buf2);
    errno_assert (rc == 0);


    rc = sp_close (sc);
    errno_assert (rc == 0);
    rc = sp_close (sb);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);

    return 0;
}

