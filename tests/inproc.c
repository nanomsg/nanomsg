/*
    Copyright (c) 2012 250bpm s.r.o.

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
#include "../src/inproc.h"

#include "../src/utils/err.c"

/*  Tests inproc transport. */

#define SOCKET_ADDRESS "inproc://test"

int main ()
{
    int rc;
    int sb;
    int sc;
    int i;
    char buf [3];

    /*  Create a simple topology. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Try a duplicate bind. It should fail. */
    rc = nn_bind (sc, SOCKET_ADDRESS);
    nn_assert (rc < 0 && errno == EADDRINUSE);

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {

        rc = nn_send (sc, "ABC", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_send (sb, "DEFG", 4, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 4);

        rc = nn_recv (sc, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 4);
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        rc = nn_send (sc, "XYZ", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);
    }
    for (i = 0; i != 100; ++i) {
        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);
    }

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

