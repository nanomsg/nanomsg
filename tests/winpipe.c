/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright 2017 Garrett D'Amore <garrett@damore.org>
    Copyright 2026 Staysail Systems, Inc.

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
#include "../src/unix.h"
#include "../src/winpipe.h"
#include "testutil.h"

static void roundtrip (char *bindaddr, char *connectaddr)
{
    int sb;
    int sc;
    int timeout = 2000;
    int size = 8192;
    int opt;
    size_t optlen;

    sb = test_socket (AF_SP, NN_PAIR);
    sc = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (sb, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sc, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sb, NN_WINPIPE, NN_WINPIPE_OUTBUFSZ, &size, sizeof (size));
    optlen = sizeof (opt);
    nn_assert (nn_getsockopt (sb, NN_IPC, NN_IPC_OUTBUFSZ, &opt, &optlen) == 0);
    nn_assert (opt == size);
    test_bind (sb, bindaddr);
    test_connect (sc, connectaddr);
    test_send (sc, "pipe");
    test_recv (sb, "pipe");
    test_close (sc);
    test_close (sb);
}

int main (void)
{
    int pipe;
    int unixsock;
    int pc;
    int uc;
    int timeout = 2000;
    int rc;

    nn_assert (NN_IPC == NN_WINPIPE);
    nn_assert (NN_UNIX != NN_WINPIPE);
    roundtrip ("winpipe://test.winpipe", "winpipe://test.winpipe");
    roundtrip ("winpipe://test.winpipe", "ipc://test.winpipe");
    roundtrip ("ipc://test.winpipe", "winpipe://test.winpipe");

    /*  The same name refers to separate socket and named-pipe endpoints. */
    pipe = test_socket (AF_SP, NN_PAIR);
    unixsock = test_socket (AF_SP, NN_PAIR);
    pc = test_socket (AF_SP, NN_PAIR);
    uc = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (pipe, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (unixsock, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (pc, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (uc, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    rc = nn_bind (pipe, "winpipe://");
    nn_assert (rc < 0 && nn_errno () == EINVAL);
    rc = nn_connect (pc, "winpipe://bad\\name");
    nn_assert (rc < 0 && nn_errno () == EINVAL);
    test_bind (pipe, "winpipe://test.separate");
    test_bind (unixsock, "unix://test.separate");
    test_connect (pc, "ipc://test.separate");
    test_connect (uc, "unix://test.separate");
    test_send (pc, "named pipe");
    test_send (uc, "unix socket");
    test_recv (pipe, "named pipe");
    test_recv (unixsock, "unix socket");
    test_close (uc);
    test_close (pc);
    test_close (unixsock);
    test_close (pipe);
    return 0;
}
