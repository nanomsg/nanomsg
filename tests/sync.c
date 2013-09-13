/*
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
#include "../src/sync.h"

#include "testutil.h"

#define SOCKET_ADDRESS "inproc://a"

int main ()
{
    int rc;
    int master;
    int mirror;
    char buf [3];

    /*  Check whether unitialised state is sent to the mirror when it
        connects. */
    master = test_socket (AF_SP, NN_MASTER);
    test_send (master, "A");
    test_bind (master, SOCKET_ADDRESS);
    mirror = test_socket (AF_SP, NN_MIRROR);
    test_connect (mirror, SOCKET_ADDRESS);
    test_recv (mirror, "A");

    /*  Test whether changing of the state results in updating the mirror. */
    test_send (master, "B");
    test_recv (mirror, "B");

    test_close (mirror);
    test_close (master);

    return 0;
}

