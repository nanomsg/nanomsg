/*
    Copyright (c) 2013 250bpm s.r.o.

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
#include "../src/fanin.h"
#include "../src/inproc.h"
#include "../src/ipc.h"
#include "../src/tcp.h"
#include "../src/utils/err.c"

#define SOCKET_ADDRESS_INPROC "inproc://a"
#define SOCKET_ADDRESS_IPC "ipc://test.ipc"
#define SOCKET_ADDRESS_TCP "tcp://127.0.0.1:5555"

/*  This test checks whether the library prevents connecting sockets from
    non-compatible protocols. */

int main ()
{
    int rc;
    int pair;
    int sink;
    int timeo;

    /*  Inproc: Bind first, connect second. */
    pair = nn_socket (AF_SP, NN_PAIR);
    errno_assert (pair != -1);
    rc = nn_bind (pair, SOCKET_ADDRESS_INPROC);
    errno_assert (rc >= 0);
    sink = nn_socket (AF_SP, NN_SINK);
    errno_assert (sink != -1);
    rc = nn_connect (sink, SOCKET_ADDRESS_INPROC);
    errno_assert (rc >= 0);
    timeo = 100;
    rc = nn_setsockopt (pair, NN_SOL_SOCKET, NN_SNDTIMEO,
        &timeo, sizeof (timeo));
    rc = nn_send (pair, "ABC", 3, 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);
    rc = nn_close (sink);
    errno_assert (rc == 0);
    rc = nn_close (pair);
    errno_assert (rc == 0);

    /*  Inproc: Connect first, bind second. */
    sink = nn_socket (AF_SP, NN_SINK);
    errno_assert (sink != -1);
    rc = nn_connect (sink, SOCKET_ADDRESS_INPROC);
    errno_assert (rc >= 0);
    pair = nn_socket (AF_SP, NN_PAIR);
    errno_assert (pair != -1);
    rc = nn_bind (pair, SOCKET_ADDRESS_INPROC);
    errno_assert (rc >= 0);
    timeo = 100;
    rc = nn_setsockopt (pair, NN_SOL_SOCKET, NN_SNDTIMEO,
        &timeo, sizeof (timeo));
    rc = nn_send (pair, "ABC", 3, 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);
    rc = nn_close (sink);
    errno_assert (rc == 0);
    rc = nn_close (pair);
    errno_assert (rc == 0);

    /*  IPC */
    pair = nn_socket (AF_SP, NN_PAIR);
    errno_assert (pair != -1);
    rc = nn_bind (pair, SOCKET_ADDRESS_IPC);
    errno_assert (rc >= 0);
    sink = nn_socket (AF_SP, NN_SINK);
    errno_assert (sink != -1);
    rc = nn_connect (sink, SOCKET_ADDRESS_IPC);
    errno_assert (rc >= 0);
    timeo = 100;
    rc = nn_setsockopt (pair, NN_SOL_SOCKET, NN_SNDTIMEO,
        &timeo, sizeof (timeo));
    rc = nn_send (pair, "ABC", 3, 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);
    rc = nn_close (sink);
    errno_assert (rc == 0);
    rc = nn_close (pair);
    errno_assert (rc == 0);

    /*  TCP */
    pair = nn_socket (AF_SP, NN_PAIR);
    errno_assert (pair != -1);
    rc = nn_bind (pair, SOCKET_ADDRESS_TCP);
    errno_assert (rc >= 0);
    sink = nn_socket (AF_SP, NN_SINK);
    errno_assert (sink != -1);
    rc = nn_connect (sink, SOCKET_ADDRESS_TCP);
    errno_assert (rc >= 0);
    timeo = 100;
    rc = nn_setsockopt (pair, NN_SOL_SOCKET, NN_SNDTIMEO,
        &timeo, sizeof (timeo));
    rc = nn_send (pair, "ABC", 3, 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);
    rc = nn_close (sink);
    errno_assert (rc == 0);
    rc = nn_close (pair);
    errno_assert (rc == 0);

    return 0;
}

