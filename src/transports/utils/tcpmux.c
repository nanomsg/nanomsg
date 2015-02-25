/*
    Copyright (c) 2014 Martin Sustrik  All rights reserved.

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

#include "../utils/err.h"
#include "../utils/wire.h"

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "tcpmux.h"

int tcpmux_listen (int port, const char *service)
{
    int rc;
    int s;
    char ipcaddr [32];
    struct sockaddr_un unaddr;
    unsigned char buf [2];
    size_t sz;
    ssize_t ssz;

    /*  Make a connection to wsmux daemon. */
    s = socket (AF_UNIX, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    snprintf (ipcaddr, sizeof (ipcaddr), "/tmp/tcpmux-%d.ipc", port);
    nn_assert (strlen (ipcaddr) < sizeof (unaddr.sun_path));
    unaddr.sun_family = AF_UNIX;
    strcpy (unaddr.sun_path, ipcaddr);
    rc = connect (s, (struct sockaddr*) &unaddr, sizeof (unaddr));
    if (rc != 0)
        return -1;

    /* Send the connection header. */
    sz = strlen (service);
    nn_puts (buf, sz);
    ssz = send (s, buf, 2, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == 2);
    ssz = send (s, service, sz, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == sz);

    /*  Return the connection file descriptor to the user. */
    return s;
}

int tcpmux_accept (int s)
{
    int rc;
    ssize_t ssz;
    char c;
    struct iovec iov;
    struct msghdr hdr;
    unsigned char buf [256];
    struct cmsghdr *cmsg;

    iov.iov_base = &c;
    iov.iov_len = 1;
    memset (&hdr, 0, sizeof (hdr));
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = buf;
    hdr.msg_controllen = sizeof (buf);

    ssz = recvmsg (s, &hdr, 0);
    if (ssz < 0)
        return -1;
    nn_assert (ssz == 1);

    cmsg = CMSG_FIRSTHDR (&hdr);
    while (1) {
        nn_assert (cmsg);
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SP_HDR)
            break;
        cmsg = CMSG_NXTHDR (&hdr, cmsg);
    }
    nn_assert (cmsg->cmsg_len == CMSG_LEN (sizeof (int)));
    return *((int*) NN_CMSG_DATA (cmsg));
}

int tcpmux_connect (int s, const struct sockaddr *addr,
    socklen_t addrlen, const char *service)
{
    int rc;
    size_t sz;
    ssize_t ssz;
    char c;
    char crlf [2];

    /*  Establish the connection. */
    rc = connect (s, addr, addrlen);
    if (rc != 0)
       return -1;

    /*  Send the TCPMUX header. */
    sz = strlen (service);
    ssz = send (s, service, sz, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == sz);
    ssz = send (s, "\x0d\x0a", 2, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == 2);

    /*  Receive TCPMUX reply. */
    ssz = recv (s, &c, 1, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == 1);

    /*  Failure. */
    if (c != '+') {
        close (s);
        errno = ECONNREFUSED;
        return -1;
    }

    /*  Success. Get rid of CRLF. */
    ssz = recv (s, crlf, 2, 0);
    errno_assert (ssz >= 0);
    nn_assert (ssz == 2);
    nn_assert (crlf [0] == 0x0d && crlf [1] == 0x0a);

    return 0;
}
