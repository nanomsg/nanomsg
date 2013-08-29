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
#include "../src/tcp.h"
#include "../src/reqrep.h"

#include "../src/utils/err.c"

int main ()
{
    int s;
    int rc;
    int eid;

    /*  Run endpoint shutdown and socket shutdown in parallel. */
    s = nn_socket (AF_SP, NN_REQ);
    errno_assert (s >= 0);
    eid = nn_connect (s, "tcp://127.0.0.1:5590");
    errno_assert (eid >= 0);
    rc = nn_shutdown (s, eid);
    errno_assert (rc == 0);
    rc = nn_close (s);
    errno_assert (rc == 0);

    return 0;
}

