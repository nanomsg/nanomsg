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

#if defined NN_HAVE_POLL

#include "../nn.h"

#include "../utils/err.h"
#include "../utils/fast.h"

#include <poll.h>
#include <string.h>

/*  Private functions. */
static void nn_device_mvmsg (int from, int to);

int nn_device (int s1, int s2)
{
    int rc;
    int s1in;
    int s1out;
    int s1err;
    int s2in;
    int s2out;
    int s2err;
    size_t optsz;
    struct pollfd pfd [6];

    /*  Get file descriptors to poll on. */
    /*  TODO: To avoid using too much resource, some internal polling mechanism
        should be used here instead of file descriptors. */
    optsz = sizeof (s1in);
    rc = nn_getsockopt (s1, NN_SOL_SOCKET, NN_RCVFD, &s1in, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s1in));
    optsz = sizeof (s1out);
    rc = nn_getsockopt (s1, NN_SOL_SOCKET, NN_SNDFD, &s1out, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s1out));
    optsz = sizeof (s1err);
    rc = nn_getsockopt (s1, NN_SOL_SOCKET, NN_ERRFD, &s1err, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s1err));
    optsz = sizeof (s2in);
    rc = nn_getsockopt (s2, NN_SOL_SOCKET, NN_RCVFD, &s2in, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s2in));
    optsz = sizeof (s2out);
    rc = nn_getsockopt (s2, NN_SOL_SOCKET, NN_SNDFD, &s2out, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s2out));
    optsz = sizeof (s2err);
    rc = nn_getsockopt (s2, NN_SOL_SOCKET, NN_ERRFD, &s2err, &optsz);
    if (nn_slow (rc != 0))
        return -1;
    nn_assert (optsz == sizeof (s2err));

    /*  Initialise the pollset. */
    pfd [0].fd = s1in;
    pfd [0].events = POLLIN;
    pfd [1].fd = s1out;
    pfd [1].events = POLLIN;
    pfd [2].fd = s1err;
    pfd [2].events = POLLIN;
    pfd [3].fd = s2in;
    pfd [3].events = POLLIN;
    pfd [4].fd = s2out;
    pfd [4].events = POLLIN;
    pfd [5].fd = s2err;
    pfd [5].events = POLLIN;

    while (1) {

        /*  Wait for network events. */
        rc = poll (pfd, 6, -1);
        errno_assert (rc >= 0);
        if (nn_slow (rc < 0 && errno == EINTR))
            return -1;
        nn_assert (rc != 0);

        /*  Process the eventual errors. */
        if (nn_slow (pfd [2].revents & POLLIN || pfd [5].revents & POLLIN)) {
            errno = ETERM;
            return -1;
        }

        /*  Process the events. */
        if (pfd [0].revents & POLLIN)
            pfd [0].events = 0;
        if (pfd [1].revents & POLLIN)
            pfd [1].events = 0;
        if (pfd [3].revents & POLLIN)
            pfd [3].events = 0;
        if (pfd [4].revents & POLLIN)
            pfd [4].events = 0;

        /*  If possible, pass the message from s1 to s2. */
        if (pfd [0].events == 0 && pfd [4].events == 0) {
            nn_device_mvmsg (s1, s2);
            pfd [0].events = POLLIN;
            pfd [4].events = POLLIN;
        }

        /*  If possible, pass the message from s2 to s1. */
        if (pfd [3].events == 0 && pfd [1].events == 0) {
            nn_device_mvmsg (s2, s1);
            pfd [3].events = POLLIN;
            pfd [1].events = POLLIN;
        }
    }
}

static void nn_device_mvmsg (int from, int to)
{
    int rc;
    void *body;
    void *control;
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    iov.iov_base = &body;
    iov.iov_len = NN_MSG;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = &control;
    hdr.msg_controllen = NN_MSG;
    rc = nn_recvmsg (from, &hdr, NN_DONTWAIT);
    errno_assert (rc >= 0);
    rc = nn_sendmsg (to, &hdr, NN_DONTWAIT);
    errno_assert (rc >= 0);
}

#endif

