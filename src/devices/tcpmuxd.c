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

#include "../nn.h"

#include "../utils/thread.h"
#include "../utils/attr.h"
#include "../utils/err.h"
#include "../utils/int.h"
#include "../utils/cont.h"
#include "../utils/wire.h"
#include "../utils/alloc.h"
#include "../utils/list.h"
#include "../utils/mutex.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <stddef.h>
#include <ctype.h>

struct ipc_connection {
    int fd;
    char *service;
    struct nn_list_item item;
};

struct ipc_connections {
    struct nn_mutex sync;
    struct nn_list connections;
};

struct ipc_connections ipcs;

static int send_fd (int s, int fd)
{
    int rc;
    struct iovec iov;
    char c = 0;
    struct msghdr msg;
    char control [sizeof (struct cmsghdr) + 10];
    struct cmsghdr *cmsg;

    /*  Compose the message. We'll send one byte long dummy message
        accompanied with the fd.*/
    iov.iov_base = &c;
    iov.iov_len = 1;
    memset (&msg, 0, sizeof (msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof (control);

    /*  Attach the file descriptor to the message. */
    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (sizeof (fd));
    int *data = (int*) CMSG_DATA (cmsg);
    *data = fd;

    /*  Adjust the size of the control to match the data. */
    msg.msg_controllen = cmsg->cmsg_len;

    /*  Pass the file descriptor to the registered process. */
    rc = sendmsg (s, &msg, 0);
    if (rc < 0)
        return -1;
    nn_assert (rc == 1);

    return 0;
}

static void ipc_listener_routine (void *arg)
{
    int rc;
    char ipcaddr [32];
    struct sockaddr_un unaddr;
    int listener;
    int conn;
    unsigned char buf [2];
    ssize_t ssz;
    uint16_t sz;
    struct ipc_connection *ipcc;
    int i;
    
    /*  Start listening for AF_UNIX connections. */
    snprintf (ipcaddr, sizeof (ipcaddr), "/tmp/tcpmux-%d.ipc", *((int*) arg));
    unlink (ipcaddr);
    listener = socket (AF_UNIX, SOCK_STREAM, 0);
    errno_assert (listener >= 0);
    nn_assert (strlen (ipcaddr) < sizeof (unaddr.sun_path));
    unaddr.sun_family = AF_UNIX;
    strcpy (unaddr.sun_path, ipcaddr);
    rc = bind (listener, (struct sockaddr*) &unaddr, sizeof (unaddr));
    errno_assert (rc == 0);
    rc = listen (listener, 100);
    errno_assert (rc == 0);

    while (1) {

        /*  Accept new IPC connection. */
        conn = accept (listener, NULL, NULL);
        if (conn < 0 && errno == ECONNABORTED)
            continue;
        errno_assert (conn >= 0);

        /*  Create new connection entry. */
        ipcc = nn_alloc (sizeof (struct ipc_connection), "ipc_connection");
        nn_assert (ipcc);
        ipcc->fd = conn;
        nn_list_item_init (&ipcc->item);    

        /*  Read the connection header. */
        ssz = recv (conn, buf, 2, 0);
        errno_assert (ssz >= 0);
        nn_assert (ssz == 2);
        sz = nn_gets (buf);
        ipcc->service = nn_alloc (sz + 1, "service");
        nn_assert (ipcc->service);
        ssz = recv (conn, ipcc->service, sz, 0);
        errno_assert (ssz >= 0);
        nn_assert (ssz == sz);
        for (i = 0; i != sz; ++i)
            ipcc->service [sz] = tolower (ipcc->service [sz]);
        ipcc->service [sz] = 0;
        
        /*  Add the entry to the global IPC connections list. */
        nn_mutex_lock (&ipcs.sync);
        nn_list_insert (&ipcs.connections, &ipcc->item,
            nn_list_end (&ipcs.connections));
        nn_mutex_unlock (&ipcs.sync);
    }
}

int nn_tcpmuxd (int port)
{
    int rc;
    struct nn_thread ipc_listener;
    int listener;
    struct sockaddr_in addr;
    int opt;
    struct nn_list_item *it;
    struct ipc_connection *ipcc;
    int conn;
    char service [256];
    int pos;
    ssize_t ssz;

    /*  Initialise the global structures. */
    nn_mutex_init (&ipcs.sync);
    nn_list_init (&ipcs.connections);

    /*  Start listening for incoming IPC connections. */
    nn_thread_init (&ipc_listener, ipc_listener_routine, &port);

    /*  Start listening for incoming TCP connections. */
    listener = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    errno_assert (listener >= 0);
    opt = 1;
    rc = setsockopt (listener, SOL_SOCKET, SO_REUSEADDR, &opt,
        sizeof (opt));
    errno_assert (rc == 0);
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (port);
    addr.sin_addr.s_addr = INADDR_ANY;
    rc = bind (listener, (struct sockaddr*) &addr, sizeof (addr));
    errno_assert (rc == 0);
    rc = listen (listener, 100);
    errno_assert (rc == 0);

    while (1) {

        /*  Accept new TCP connection. */
        conn = accept (listener, NULL, NULL);
        if (conn < 0 && errno == ECONNABORTED)
            continue;
        errno_assert (conn >= 0);

        /*  Read TCPMUX header. */
        pos = 0;
        while (1) {
            nn_assert (pos < sizeof (service));
            ssz = recv (conn, &service [pos], 1, 0);
            errno_assert (ssz >= 0);
            nn_assert (ssz == 1);
            service [pos] = tolower (service [pos]);
            if (pos > 0 && service [pos - 1] == 0x0d && service [pos] == 0x0a)
                break;
            ++pos;
        }
        service [pos - 1] = 0;
        
        /*  Check whether specified service is listening. */
        nn_mutex_lock (&ipcs.sync);
        for (it = nn_list_begin (&ipcs.connections);
              it != nn_list_end (&ipcs.connections);
              it = nn_list_next (&ipcs.connections, it)) {
            ipcc = nn_cont (it, struct ipc_connection, item);
            if (strcmp (service, ipcc->service) == 0)
                break;
        }

        /* If no one is listening, tear down the connection. */
        if (it == nn_list_end (&ipcs.connections)) {
            nn_mutex_unlock (&ipcs.sync);
            ssz = send (conn, "-Service not available.\x0d\x0a", 25, 0);
            errno_assert (ssz >= 0);
            nn_assert (ssz == 25);
            close (conn);
            continue;
        }
        nn_mutex_unlock (&ipcs.sync);

        /*  Send TCPMUX reply. */
        ssz = send (conn, "+\x0d\x0a", 3, 0);
        errno_assert (ssz >= 0);
        nn_assert (ssz == 3);

        /*  Pass the file descriptor to the listening process. */
        rc = send_fd (ipcc->fd, conn);
        errno_assert (rc == 0);
    }
}

