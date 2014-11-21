/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom

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
#include "../src/pubsub.h"
#include "../src/reqrep.h"
#include "../src/inproc.h"

#include "testutil.h"

/*  Tests inproc transport. */

#define SOCKET_ADDRESS "inproc://test"

int main ()
{
    int rc;
    int sb;
    int sc;
    int i;
    char buf [256];
    int val;
    struct nn_msghdr hdr;
    struct nn_iovec iovec;
    unsigned char body [3];
    void *control;
    struct nn_cmsghdr *cmsg;
    unsigned char *data;

    /*  Create a simple topology. */
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, SOCKET_ADDRESS);
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, SOCKET_ADDRESS);

    /*  Try a duplicate bind. It should fail. */
    rc = nn_bind (sc, SOCKET_ADDRESS);
    nn_assert (rc < 0 && errno == EADDRINUSE);

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {

        test_send (sc, "ABC");
        test_recv (sb, "ABC");
        test_send (sb, "DEFG");
        test_recv (sc, "DEFG");
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        test_send (sc, "XYZ");
    }
    for (i = 0; i != 100; ++i) {
        test_recv (sb, "XYZ");
    }

    test_close (sc);
    test_close (sb);

    /*  Test whether queue limits are observed. */
    sb = test_socket (AF_SP, NN_PAIR);
    val = 200;
    rc = nn_setsockopt (sb, NN_SOL_SOCKET, NN_RCVBUF, &val, sizeof (val));
    errno_assert (rc == 0);
    test_bind (sb, SOCKET_ADDRESS);
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, SOCKET_ADDRESS);

    val = 200;
    rc = nn_setsockopt (sc, NN_SOL_SOCKET, NN_SNDTIMEO, &val, sizeof (val));
    errno_assert (rc == 0);
    i = 0;
    while (1) {
        rc = nn_send (sc, "0123456789", 10, 0);
        if (rc < 0 && nn_errno () == EAGAIN)
            break;
        errno_assert (rc >= 0);
        nn_assert (rc == 10);
        ++i;
    }
    nn_assert (i == 20);
    test_recv (sb, "0123456789");
    test_send (sc, "0123456789");
    rc = nn_send (sc, "0123456789", 10, 0);
    nn_assert (rc < 0 && nn_errno () == EAGAIN);
    for (i = 0; i != 20; ++i) {
        test_recv (sb, "0123456789");
    }

    /*  Make sure that even a message that doesn't fit into the buffers
        gets across. */
    for (i = 0; i != sizeof (buf); ++i)
        buf [i] = 'A';
    rc = nn_send (sc, buf, 256, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);
    rc = nn_recv (sb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 256);

    test_close (sc);
    test_close (sb);

#if 0
    /*  Test whether connection rejection is handled decently. */
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, SOCKET_ADDRESS);
    s1 = test_socket (AF_SP, NN_PAIR);
    test_connect (s1, SOCKET_ADDRESS);
    s2 = test_socket (AF_SP, NN_PAIR);
    test_connect (s2, SOCKET_ADDRESS);
    nn_sleep (100);
    test_close (s2);
    test_close (s1);
    test_close (sb);
#endif

    /* Check whether SP message header is transferred correctly. */
    sb = test_socket (AF_SP_RAW, NN_REP);
    test_bind (sb, SOCKET_ADDRESS);
    sc = test_socket (AF_SP, NN_REQ);
    test_connect (sc, SOCKET_ADDRESS);

    test_send (sc, "ABC");

    iovec.iov_base = body;
    iovec.iov_len = sizeof (body);
    hdr.msg_iov = &iovec;
    hdr.msg_iovlen = 1;
    hdr.msg_control = &control;
    hdr.msg_controllen = NN_MSG;
    rc = nn_recvmsg (sb, &hdr, 0);
    errno_assert (rc == 3);

    cmsg = NN_CMSG_FIRSTHDR (&hdr);
    while (1) {
        nn_assert (cmsg);
        if (cmsg->cmsg_level == PROTO_SP && cmsg->cmsg_type == SP_HDR)
            break;
        cmsg = NN_CMSG_NXTHDR (&hdr, cmsg);
    }
    nn_assert (cmsg->cmsg_len == NN_CMSG_SPACE (8));
    data = NN_CMSG_DATA (cmsg);
    nn_assert (!(data[0] & 0x80));
    nn_assert (data[4] & 0x80);

    nn_freemsg (control);

    test_close (sc);
    test_close (sb);
    
    return 0;
}

