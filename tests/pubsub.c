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
#include "../src/pubsub.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

#define SOCKET_ADDRESS "inproc://a"

int main ()
{
    int rc;
    int pub;
    int sub1;
    int sub2;
    char buf [3];

    pub = nn_socket (AF_SP, NN_PUB);
    errno_assert (pub != -1);
    rc = nn_bind (pub, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    sub1 = nn_socket (AF_SP, NN_SUB);
    errno_assert (sub1 != -1);
    rc = nn_setsockopt (sub1, NN_SUB, NN_SUBSCRIBE, "", 0);
    errno_assert (rc == 0);
    rc = nn_connect (sub1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    sub2 = nn_socket (AF_SP, NN_SUB);
    errno_assert (sub2 != -1);
    rc = nn_setsockopt (sub2, NN_SUB, NN_SUBSCRIBE, "", 0);
    errno_assert (rc == 0);
    rc = nn_connect (sub2, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Wait till connections are established to prevent message loss. */
    nn_sleep (10);

    rc = nn_send (pub, "0123456789012345678901234567890123456789", 40, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 40);

    rc = nn_recv (sub1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 40);
    rc = nn_recv (sub2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 40);

    rc = nn_close (pub);
    errno_assert (rc == 0);
    rc = nn_close (sub1);
    errno_assert (rc == 0);    
    rc = nn_close (sub2);
    errno_assert (rc == 0);

    return 0;
}

