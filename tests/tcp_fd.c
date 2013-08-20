/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

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
#include "../src/pubsub.h"
#include "../src/tcp.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*  Tests TCP transport created from file descriptor. */

#define SOCKET_HOST "127.0.0.1"
#define SOCKET_PORT 5555

int main ()
{

#if !defined NN_HAVE_WINDOWS
    int list_fd;
    int conn_fd;
    int rc;
    int sb;
    int sc;
    int i;
    char addrbuf [64];
    char buf [3];
    int opt;
    size_t sz;
    struct sockaddr_in saddr;

    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr (SOCKET_HOST);
    saddr.sin_port = htons (SOCKET_PORT);

    list_fd = socket (AF_INET, SOCK_STREAM, 0);
    errno_assert (list_fd != -1);

    rc = bind (list_fd, (struct sockaddr *)&saddr, sizeof(saddr));
    errno_assert (rc != -1);
    rc = listen (list_fd, 1);
    errno_assert (rc != -1);

    conn_fd = socket (AF_INET, SOCK_STREAM, 0);
    errno_assert (conn_fd != -1);

    rc = connect (conn_fd, (struct sockaddr *)&saddr, sizeof(saddr));
    errno_assert (rc != -1);

    sb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = sprintf(addrbuf, "tcp+fd://%d", list_fd);
    errno_assert (rc >= 0 && rc < sizeof(addrbuf));
    rc = nn_bind (sb, addrbuf);
    errno_assert (rc >= 0);

    sc = nn_socket (AF_SP, NN_PAIR);
    errno_assert (sb != -1);
    rc = sprintf(addrbuf, "tcp+fd://%d", conn_fd);
    errno_assert (rc >= 0 && rc < sizeof(addrbuf));
    rc = nn_connect (sc, addrbuf);
    errno_assert (rc >= 0);

    /*  Ping-pong test. */
    for (i = 0; i != 100; ++i) {

        rc = nn_send (sc, "ABC", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_send (sb, "DEF", 3, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);

        rc = nn_recv (sc, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 3);
    }

    /*  Batch transfer test. */
    for (i = 0; i != 100; ++i) {
        rc = nn_send (sc, "0123456789012345678901234567890123456789", 40, 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);
    }
    for (i = 0; i != 100; ++i) {
        rc = nn_recv (sb, buf, sizeof (buf), 0);
        errno_assert (rc >= 0);
        nn_assert (rc == 40);
    }

    rc = nn_close (sc);
    errno_assert (rc == 0);
    rc = nn_close (sb);
    errno_assert (rc == 0);

#endif

    return 0;
}

