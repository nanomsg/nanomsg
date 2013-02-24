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
#include "../src/utils/thread.c"
#include "../src/utils/sleep.c"

#include <sys/select.h>

/*  Test of polling via NN_SNDFD/NN_RCVFD mechanism. */

#define SOCKET_ADDRESS "inproc://a"

int sc;

void routine1 (void *arg)
{
   int rc;

   nn_sleep (10);
   rc = nn_send (sc, "ABC", 3, 0);
   errno_assert (rc >= 0);
   nn_assert (rc == 3);
}

void routine2 (void *arg)
{
   nn_sleep (10);
   nn_term ();
}

#define IN 1
#define OUT 2

int getevents (int s, int events, int timeout)
{
    int rc;
    fd_set pollset;
#if defined NN_HAVE_WINDOWS
    SOCKET rcvfd;
    SOCKET sndfd;
#else
    int rcvfd;
    int sndfd;
    int maxfd;
#endif
    size_t fdsz;
    struct timeval tv;
    int revents;

    maxfd = 0;
    FD_ZERO (&pollset);

    if (events & IN) {
        fdsz = sizeof (rcvfd);
        rc = nn_getsockopt (s, NN_SOL_SOCKET, NN_RCVFD, (char*) &rcvfd, &fdsz);
        errno_assert (rc == 0);
        nn_assert (fdsz == sizeof (rcvfd));
        FD_SET (rcvfd, &pollset);
        if (rcvfd + 1 > maxfd)
            maxfd = rcvfd + 1;
    }

    if (events & OUT) {
        fdsz = sizeof (sndfd);
        rc = nn_getsockopt (s, NN_SOL_SOCKET, NN_SNDFD, (char*) &sndfd, &fdsz);
        errno_assert (rc == 0);
        nn_assert (fdsz == sizeof (sndfd));
        FD_SET (sndfd, &pollset);
        if (sndfd + 1 > maxfd)
            maxfd = sndfd + 1;
    }

    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
    }
#if defined NN_HAVE_WINDOWS
    rc = select (0, &pollser, NULL, NULL, timeout < 0 ? NULL : &tv);
    wsa_assert (rc != SOCKET_ERROR);
#else
    rc = select (maxfd, &pollset, NULL, NULL, timeout < 0 ? NULL : &tv);
    errno_assert (rc >= 0);
#endif
    revents = 0;
    if ((events & IN) && FD_ISSET (rcvfd, &pollset))
        revents |= IN;
    if ((events & OUT) && FD_ISSET (sndfd, &pollset))
        revents |= OUT;
    return revents;
}

int main ()
{
    int rc;
    int sb;
    char buf [3];
    struct nn_thread thread;

    /*  Create a simple topology. */
    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = nn_bind (sb, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sc != -1);
    rc = nn_connect (sc, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Check the initial state of the socket. */
    rc = getevents (sb, IN | OUT, 1000);
    nn_assert (rc == OUT);

    /*  Poll for IN when there's no message available. The call should
        time out. */
    rc = getevents (sb, IN, 10);
    nn_assert (rc == 0);

    /*  Send a message and start polling. This time IN event should be
        signaled. */
    rc = nn_send (sc, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = getevents (sb, IN, 1000);
    nn_assert (rc == IN);

    /*  Receive the message and make sure that IN is no longer signaled. */
    rc = nn_recv (sb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = getevents (sb, IN, 10);
    nn_assert (rc == 0);

    /*  Check signalling from a different thread. */
    nn_thread_init (&thread, routine1, NULL);
    rc = getevents (sb, IN, 1000);
    nn_assert (rc == IN);
    rc = nn_recv (sb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    nn_thread_term (&thread);

    /*  Check terminating the library from a different thread. */
    nn_thread_init (&thread, routine2, NULL);
    rc = getevents (sb, IN, 1000);
    nn_assert (rc == IN);
    rc = nn_recv (sb, buf, sizeof (buf), 0);
    nn_assert (rc < 0 && nn_errno () == ETERM);
    nn_thread_term (&thread);

    /*  Clean up. */
    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

    return 0;
}

