/*
    Copyright (c) 2013 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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
#include "../src/pipeline.h"

#include "../src/utils/err.c"

#define SOCKET_ADDRESS_A "inproc://a"
#define SOCKET_ADDRESS_B "inproc://b"

int main ()
{
    int rc;
    int push;
    int pull1;
    int pull2;
    int sndprio;
    char buf [3];

    pull1 = nn_socket (AF_SP, NN_PULL);
    errno_assert (pull1 != -1);
    rc = nn_bind (pull1, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    pull2 = nn_socket (AF_SP, NN_PULL);
    errno_assert (pull2 != -1);
    rc = nn_bind (pull2, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);
    push = nn_socket (AF_SP, NN_PUSH);
    errno_assert (push != -1);
    sndprio = 1;
    rc = nn_setsockopt (push, NN_SOL_SOCKET, NN_SNDPRIO,
        &sndprio, sizeof (sndprio));
    errno_assert (rc == 0);
    rc = nn_connect (push, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    sndprio = 2;
    rc = nn_setsockopt (push, NN_SOL_SOCKET, NN_SNDPRIO,
        &sndprio, sizeof (sndprio));
    errno_assert (rc == 0);
    rc = nn_connect (push, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);

    rc = nn_send (push, "ABC", 3, 0);
    errno_assert (rc == 3);
    rc = nn_send (push, "DEF", 3, 0);
    errno_assert (rc == 3);
    rc = nn_recv (pull1, buf, sizeof (buf), 0);
    errno_assert (rc == 3);
    rc = nn_recv (pull1, buf, sizeof (buf), 0);
    errno_assert (rc == 3);

    rc = nn_close (pull1);
    errno_assert (rc == 0);
    rc = nn_close (push);
    errno_assert (rc == 0);
    rc = nn_close (pull2);
    errno_assert (rc == 0);

    return 0;
}

