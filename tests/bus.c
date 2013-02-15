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
#include "../src/bus.h"
#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

int main ()
{
    int rc;
    int bus1;
    int bus2;
    int bus3;
    char buf [3];

    /*  Create a simple bus topology consisting of 3 nodes. */
    bus1 = nn_socket (AF_SP, NN_BUS);
    errno_assert (bus1 != -1);
    rc = nn_bind (bus1, "inproc://a");
    errno_assert (rc >= 0);
    bus2 = nn_socket (AF_SP, NN_BUS);
    errno_assert (bus2 != -1);
    rc = nn_bind (bus2, "inproc://b");
    errno_assert (rc >= 0);
    rc = nn_connect (bus2, "inproc://a");
    errno_assert (rc >= 0);
    bus3 = nn_socket (AF_SP, NN_BUS);
    errno_assert (bus3 != -1);
    rc = nn_connect (bus3, "inproc://a");
    errno_assert (rc >= 0);
    rc = nn_connect (bus3, "inproc://b");
    errno_assert (rc >= 0);

    /*  Send a message from each node. */
    rc = nn_send (bus1, "A", 1, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 1);
    rc = nn_send (bus2, "AB", 2, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 2);
    rc = nn_send (bus3, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    /*  Check that two messages arrived at each node. */
    rc = nn_recv (bus1, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 2 || rc == 3);
    rc = nn_recv (bus1, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 2 || rc == 3);
    rc = nn_recv (bus2, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 1 || rc == 3);
    rc = nn_recv (bus2, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 1 || rc == 3);
    rc = nn_recv (bus3, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 1 || rc == 2);
    rc = nn_recv (bus3, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 1 || rc == 2);

    /*  Wait till both connections are established. */
    nn_sleep (10);

    rc = nn_close (bus3);
    errno_assert (rc == 0);
    rc = nn_close (bus2);
    errno_assert (rc == 0);    
    rc = nn_close (bus1);
    errno_assert (rc == 0);

    return 0;
}

