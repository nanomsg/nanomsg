/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.

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

#include "testutil.h"

#include <string.h>

#define SOCKET_ADDRESS "inproc://a"
#define SOCKET_ADDRESS_TCP "tcp://127.0.0.1:5557"

char longdata[1 << 20];

int main ()
{
    int rc;
    int sb;
    int sc;
    unsigned char *buf1, *buf2;
    int i;
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, SOCKET_ADDRESS);

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

    test_close (sc);
    test_close (sb);

    /*  Test receiving of large message  */

    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, SOCKET_ADDRESS_TCP);
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, SOCKET_ADDRESS_TCP);

    for (i = 0; i < (int) sizeof (longdata); ++i)
        longdata[i] = '0' + (i % 10);
    longdata [sizeof (longdata) - 1] = 0;
    test_send (sb, longdata);

    rc = nn_recv (sc, &buf2, NN_MSG, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == sizeof (longdata) - 1);
    nn_assert (buf2);
    for (i = 0; i < (int) sizeof (longdata) - 1; ++i)
        nn_assert (buf2 [i] == longdata [i]);
    rc = nn_freemsg (buf2);
    errno_assert (rc == 0);

    test_close (sc);
    test_close (sb);

    /* Check whether a multiple null-terminated buffer message */
    /* is received in the same number of buffers */
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, SOCKET_ADDRESS_TCP);
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, SOCKET_ADDRESS_TCP);
    
    for (i = 0; i < (int) sizeof (longdata); ++i)
        longdata[i] = '0' + (i % 10);
    longdata [sizeof (longdata) - 1] = 0;

    buf1 = nn_allocmsg(sizeof (longdata), 0);
    strcpy (buf1, longdata);
    struct nn_msghdr send_msg;
    struct nn_iovec send_iovecs[2];
    send_iovecs [0].iov_base = &buf1;
    send_iovecs [0].iov_len = NN_MSG;
    send_iovecs [1].iov_base = &buf1;
    send_iovecs [1].iov_len = NN_MSG;
    memset (&send_msg, 9, sizeof (send_msg));
    send_msg.msg_iov = send_iovecs;
    send_msg.msg_iovlen = 2;
    send_msg.msg_control = NULL;
    rc = nn_sendmsg(sc, &send_msg, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 2 * sizeof (longdata));
    
    struct nn_msghdr recv_msg;
    struct nn_iovec recv_iovecs[2];
    recv_iovecs [0].iov_base = NULL;
    recv_iovecs [0].iov_len = NN_MSG;
    recv_iovecs [1].iov_base = NULL;
    recv_iovecs [1].iov_len = NN_MSG;
    memset (&recv_msg, 9, sizeof (recv_msg));
    recv_msg.msg_iov = recv_iovecs;
    recv_msg.msg_iovlen = 2;
    recv_msg.msg_control = NULL;
    rc = nn_recvmsg(sb, &recv_msg, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 2 * sizeof (longdata));
    nn_assert (recv_iovecs [0].iov_len = sizeof (longdata));
    nn_assert (recv_iovecs [1].iov_len = sizeof (longdata));
    nn_assert (strcmp ((const char*)recv_iovecs [0].iov_base, buf1) == 0);
    nn_assert (strcmp ((const char*)recv_iovecs [1].iov_base, buf1) == 0);
    
    test_close(sb);
    test_close(sc);
    
    return 0;
}

