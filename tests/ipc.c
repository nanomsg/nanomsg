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

#include "../src/nn.h"
#include "../src/pair.h"
#include "../src/pubsub.h"
#include "../src/ipc.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

/*  Tests IPC transport. */

#define SOCKET_ADDRESS "ipc://test.ipc"

int main ()
{
#if !defined NN_HAVE_WINDOWS
    int rc;
    int sb;
    int sc;
    int i;
    char buf [3];

    /*  Try closing a IPC socket while it not connected. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    rc = nn_close (sc);
    errno_assert (rc == 0);

    /*  Open the socket anew. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Leave enough time for at least on re-connect attempt. */
    nn_sleep (200);

    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Ping-pong test. */
    for (i = 0; i != 1; ++i) {

        rc = nn_send (sc, "0123456789012345678901234567890123456789", 40, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);

        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);

        rc = nn_send (sb, "0123456789012345678901234567890123456789", 40, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);

        rc = nn_recv (sc, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);
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
//        nn_assert (rc == 3);
    }

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

#endif

    return 0;
}

