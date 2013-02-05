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
#include "../src/inproc.h"

#include "../src/utils/err.c"

#include <sys/select.h>

/*  Test of polling via NN_SNDFD and NN_RCVFD mechanism. */

int main ()
{
    int rc;
    int sb;
    int sc;
    fd_set pollset;
#if defined NN_HAVE_WINDOWS
    SOCKET rcvfd;
#else
    int rcvfd;
#endif
    size_t rcvfdsz;
    struct timeval tv;

    /*  Create a simple topology. */
    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, "inproc://a");
    errno_assert (rc >= 0);
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, "inproc://a");
    errno_assert (rc >= 0);

    /*  Retrieve the file descriptor for polling for inbound messages. */
    rcvfdsz = sizeof (rcvfd);
    rc = nn_getsockopt (sb, NN_SOL_SOCKET, NN_RCVFD,
        (char*) &rcvfd, &rcvfdsz);
    errno_assert (rc == 0);
    nn_assert (rcvfdsz == sizeof (rcvfd));

    /*  First poll for IN when there's no message available. The call should
        time out. */
    FD_ZERO (&pollset);
    FD_SET (rcvfd, &pollset);
    tv.tv_sec = 0;
    tv.tv_usec = 1000;
#if defined NN_HAVE_WINDOWS
    rc = select (0, &rfds, NULL, NULL, &tv);
    wsa_assert (rc != SOCKET_ERROR);
#else
    rc = select (rcvfd + 1, &pollset, NULL, NULL, &tv);
    errno_assert (rc >= 0);
#endif
    nn_assert (rc == 0);

    /*  Send a message and start polling. This time IN event should be
        signaled. */
    rc = nn_send (sc, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    FD_SET (rcvfd, &pollset);
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
#if defined NN_HAVE_WINDOWS
    rc = select (0, &rfds, NULL, NULL, &tv);
    wsa_assert (rc != SOCKET_ERROR);
#else
    rc = select (rcvfd + 1, &pollset, NULL, NULL, &tv);
    errno_assert (rc >= 0);
#endif
    nn_assert (rc == 1);

    /*  Clean up. */
    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

