/*
    Copyright (c) 2016 Franklin "Snaipe" Mathieu <franklinmathieu@gmail.com>

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
#include "../src/ipc.h"
#include "../src/reqrep.h"

#include "testutil.h"

#include <unistd.h>

/*  Test behaviour on fork. */

#define SOCKET_ADDRESS "ipc://test.ipc"
#define ALT_SOCKET_ADDRESS "ipc://test2.ipc"

int main ()
{
    int sb, sc, sb_alt, sc_alt;

    /* Do some background work */

    sb_alt = test_socket (AF_SP, NN_PAIR);
    test_bind (sb_alt, ALT_SOCKET_ADDRESS);

    sc_alt = test_socket (AF_SP, NN_PAIR);
    test_connect (sc_alt, ALT_SOCKET_ADDRESS);

    test_send (sc_alt, "0123456789012345678901234567890123456789");
    test_recv (sb_alt, "0123456789012345678901234567890123456789");
    test_send (sb_alt, "0123456789012345678901234567890123456789");

    /* Fork with a pair on each end */

    sb = test_socket (AF_SP, NN_REQ);
    test_bind (sb, SOCKET_ADDRESS);

    pid_t pid = fork();
    nn_assert(pid != -1);

    if (pid == 0) {
        nn_assert(nn_close(sb) == -1);
        nn_assert(errno == ETERM);

        /* Try to connect on the child's end */
        sc = test_socket (AF_SP, NN_REP);
        test_connect (sc, SOCKET_ADDRESS);

        /* Consume the sent data and reply */
        test_recv (sc, "0123456789012345678901234567890123456789");
        test_send (sc, "0123456789012345678901234567890123456789");

        nn_close (sc);
    } else {
        /* Send data to the child and expect a response */
        test_send (sb, "0123456789012345678901234567890123456789");
        test_recv (sb, "0123456789012345678901234567890123456789");

        nn_close (sb);

        /* Recieve late background work */
        test_recv (sc_alt, "0123456789012345678901234567890123456789");

        nn_close(sc_alt);
        nn_close(sb_alt);
    }
    return 0;
}

