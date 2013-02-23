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
#include "../src/inproc.h"

#include "../src/utils/err.c"
#include "../src/utils/thread.c"

#define SOCKET_ADDRESS_A "inproc://a"
#define SOCKET_ADDRESS_B "inproc://b"

void routine (void *arg)
{
    int rc;
    int deva;
    int devb;

    /*  Intialise the device sockets. */
    deva = nn_socket (AF_SP_RAW, NN_PAIR);
    errno_assert (deva >= 0);
    rc = nn_bind (deva, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    devb = nn_socket (AF_SP_RAW, NN_PAIR);
    errno_assert (devb >= 0);
    rc = nn_bind (devb, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);

    /*  Run the device. */
    rc = nn_device (deva, devb);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    rc = nn_close (devb);
    errno_assert (rc == 0);
    rc = nn_close (deva);
    errno_assert (rc == 0);
}

int main ()
{
    int rc;
    int enda;
    int endb;
    struct nn_thread thread;
    char buf [3];

    /*  Start the device. */
    nn_thread_init (&thread, routine, NULL);

    /*  Create two sockets to connect to the device. */
    enda = nn_socket (AF_SP, NN_PAIR);
    errno_assert (enda >= 0);
    rc = nn_connect (enda, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    endb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (endb >= 0);
    rc = nn_connect (endb, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);

    /*  Pass a pair of messages between endpoints. */
    rc = nn_send (enda, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (endb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (endb, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (enda, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    /*  Clean up. */
    rc = nn_close (endb);
    errno_assert (rc == 0);
    rc = nn_close (enda);
    errno_assert (rc == 0);
    nn_term ();
    nn_thread_term (&thread);

    return 0;
}

