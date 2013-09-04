/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.

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
#include "../src/pubsub.h"
#include "../src/tcp.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

/*  Tests TCP transport. */

#define SOCKET_ADDRESS "tcp://127.0.0.1:5555"

int main ()
{
    int rc;
    int sb;
    int sc;
    int i;
    char buf [3];
    int opt;
    size_t sz;

    /*  Try closing bound but unconnected socket. */
    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb >= 0);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc > 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    /*  Try closing a TCP socket while it not connected. At the same time
        test specifying the local address for the connection. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, "tcp://127.0.0.1;127.0.0.1:5555");
    errno_assert (rc >= 0);
    rc = nn_close (sc);
    errno_assert (rc == 0);

    /*  Open the socket anew. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);

    /*  Check NODELAY socket option. */
    sz = sizeof (opt);
    rc = nn_getsockopt (sc, NN_TCP, NN_TCP_NODELAY, &opt, &sz);
    errno_assert (rc == 0);
    nn_assert (sz == sizeof (opt));
    nn_assert (opt == 0);
    opt = 2;
    rc = nn_setsockopt (sc, NN_TCP, NN_TCP_NODELAY, &opt, sizeof (opt));
    nn_assert (rc < 0 && nn_errno () == EINVAL);
    opt = 1;
    rc = nn_setsockopt (sc, NN_TCP, NN_TCP_NODELAY, &opt, sizeof (opt));
    errno_assert (rc == 0);
    sz = sizeof (opt);
    rc = nn_getsockopt (sc, NN_TCP, NN_TCP_NODELAY, &opt, &sz);
    errno_assert (rc == 0);
    nn_assert (sz == sizeof (opt));
    nn_assert (opt == 1);

    /*  Try using invalid address strings. */
    rc = nn_connect (sc, "tcp://*:");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://*:1000000");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://*:some_port");
    nn_assert (rc < 0);
    rc = nn_connect (sc, "tcp://eth10000;127.0.0.1:5555");
    nn_assert (rc < 0);    
    errno_assert (nn_errno () == ENODEV);
    rc = nn_connect (sc, "tcp://127.0.0.1");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_bind (sc, "tcp://127.0.0.1:");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_bind (sc, "tcp://127.0.0.1:1000000");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_bind (sc, "tcp://eth10000:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == ENODEV);
    rc = nn_connect (sc, "tcp://:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://-hostname:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://abc.123.---.#:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://[::1]:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://abc.123.:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://abc...123:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);
    rc = nn_connect (sc, "tcp://.123:5555");
    nn_assert (rc < 0);
    errno_assert (nn_errno () == EINVAL);

    /*  Connect correctly. Do so before binding the peer socket. */
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Leave enough time for at least on re-connect attempt. */
    nn_sleep (200);

    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {

        rc = nn_send (sc, "ABC", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_send (sb, "DEF", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_recv (sc, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        rc = nn_send (sc, "0123456789012345678901234567890123456789", 40, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);
    }
    for (i = 0; i != 100; ++i) {
        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);
    }

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

