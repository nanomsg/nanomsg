/*
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.
    Copyright (c) 2014 250bpm s.r.o.  All rights reserved.

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
#include "../src/ws.h"

#include "../src/utils/int.h"

#include "testutil.h"

/*  Basic tests for WebSocket transport. */

int main ()
{
    int rc;
    int sb;
    int sc;
    int i;

    /*  Try closing bound but unconnected socket. */
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, "ws://*:5555");
    test_close (sb);

    /*  Try closing a TCP socket while it not connected. At the same time
        test specifying the local address for the connection. */
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, "ws://127.0.0.1:5555");
    test_close (sc);

    /*  Open the socket anew. */
    sc = test_socket (AF_SP, NN_PAIR);

    /*  Try using invalid address strings. */
    rc = nn_connect (sc, "ws://*:");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://*:1000000");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://*:some_port");
    nn_assert (rc < 0);
    rc = nn_connect (sc, "ws://eth10000;127.0.0.1:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == ENODEV);

    rc = nn_bind (sc, "ws://127.0.0.1:");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_bind (sc, "ws://127.0.0.1:1000000");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_bind (sc, "ws://eth10000:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == ENODEV);

    rc = nn_connect (sc, "ws://:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://-hostname:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://abc.123.---.#:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://[::1]:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://abc.123.:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://abc...123:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "ws://.123:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);

    test_close (sc);

    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, "ws://127.0.0.1:5555");
    sc = test_socket (AF_SP, NN_PAIR);
    test_connect (sc, "ws://127.0.0.1:5555");

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {
        test_send (sc, "ABC");
        test_recv (sb, "ABC");
        test_send (sb, "DEF");
        test_recv (sc, "DEF");
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        test_send (sc, "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789");
    }
    for (i = 0; i != 100; ++i) {
        test_recv (sb, "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789"
            "01234567890123456789012345678901234567890123456789");
    }

    test_close (sc);
    test_close (sb);

    return 0;
}

