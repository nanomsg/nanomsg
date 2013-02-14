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
#include "../src/inproc.h"

#include "../src/utils/err.c"
#include "../src/utils/thread.c"

/*  Tests the inproc transport. */

#define THREAD_COUNT 100

static void routine (void *arg)
{
    int rc;
    int s;

    s = nn_socket (AF_SP, NN_SUB);
    errno_assert (s >= 0);
    rc = nn_connect (s, "inproc://a");
    errno_assert (rc >= 0);
    rc = nn_close (s);
    errno_assert (rc == 0);
}

int main ()
{
    int rc;
    int sb;
    int sc;
    int i;
    int j;
    char buf [3];
    struct nn_thread threads [THREAD_COUNT];

    /*  Create a simple topology. */
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, "inproc://test");
    errno_assert (rc >= 0);
    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, "inproc://test");
    errno_assert (rc >= 0);

    /*  Try a duplicate bind. It should fail. */
    rc = nn_bind (sc, "inproc://test");
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

        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);
    }

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    /*  Now let's try to stress the shutdown algorithm. */
    sb = nn_socket (AF_SP, NN_PUB);
    errno_assert (sb >= 0);
    rc = nn_bind (sb, "inproc://test");
    errno_assert (rc >= 0);
    for (j = 0; j != 10; ++j) {
        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_init (&threads [i], routine, NULL);
        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_term (&threads [i]);
    }
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

