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

#if defined NN_HAVE_WINDOWS

#include "../utils/err.h"

int nn_tcpmuxd (int port)
{
    errno = EPROTONOSUPPORT;
    return -1;
}

#else

#include "../utils/thread.h"
#include "../utils/attr.h"
#include "../utils/err.h"
#include "../utils/int.h"
#include "../utils/cont.h"
#include "../utils/wire.h"
#include "../utils/alloc.h"
#include "../utils/list.h"
#include "../utils/mutex.h"
#include "../utils/closefd.h"

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/time.h>
#include <sys/un.h>
#include <stddef.h>
#include <ctype.h>
#include <poll.h>

struct nn_tcpmuxd_ctx {
    int tcp_listener;
    int ipc_listener;
    struct nn_list conns;
    struct pollfd *pfd;
    size_t pfd_size;
    size_t pfd_capacity;
    struct nn_thread thread;
};

struct nn_tcpmuxd_conn {
    int fd;
    char *service;
    struct nn_list_item item;
};

/*  Forward declarations. */
static void nn_tcpmuxd_routine (void *arg);
static void nn_tcpmuxd_disconnect (struct nn_tcpmuxd_ctx *ctx, int i);
static int nn_tcpmuxd_send_fd (int s, int fd);

int nn_tcpmuxd (int port)
{
    int rc;
    int tcp_listener;
    int ipc_listener;
    int opt;
    struct sockaddr_in tcp_addr;
    struct sockaddr_un ipc_addr;
    struct nn_tcpmuxd_ctx *ctx;

    /*  Start listening on the specified TCP port. */
    errno = 0;
    tcp_listener = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_listener < 0) { return -1; }
    opt = 1;
    rc = setsockopt (tcp_listener, SOL_SOCKET, SO_REUSEADDR, &opt,
        sizeof (opt));
    if (rc != 0) { return -1; }
    memset (&tcp_addr, 0, sizeof (tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_port = htons (port);
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    rc = bind (tcp_listener, (struct sockaddr*) &tcp_addr, sizeof (tcp_addr));
    if (rc != 0) { return -1; }
    rc = listen (tcp_listener, 100);
    if (rc != 0) { return -1; }

    /*  Start listening for incoming IPC connections. */
    ipc_addr.sun_family = AF_UNIX;
    snprintf (ipc_addr.sun_path, sizeof (ipc_addr.sun_path),
        "/tmp/tcpmux-%d.ipc", (int) port);
    unlink (ipc_addr.sun_path);
    errno = 0;
    ipc_listener = socket (AF_UNIX, SOCK_STREAM, 0);
    if (ipc_listener < 0) {
      return -1;
    }
    rc = bind (ipc_listener, (struct sockaddr*) &ipc_addr, sizeof (ipc_addr));
    if (rc != 0) { return -1; }
    rc = listen (ipc_listener, 100);
    if (rc != 0) { return -1; }

    /*  Allocate a context for the daemon. */
    ctx = nn_alloc (sizeof (struct nn_tcpmuxd_ctx), "tcpmuxd context");
    alloc_assert (ctx);
    ctx->tcp_listener = tcp_listener;
    ctx->ipc_listener = ipc_listener;
    nn_list_init (&ctx->conns);
    ctx->pfd = nn_alloc (sizeof (struct pollfd) * 16, "tcpmuxd pollfd");
    alloc_assert (ctx->pfd);
    ctx->pfd_capacity = 16;
    ctx->pfd [0].fd = tcp_listener;
    ctx->pfd [0].events = POLLIN;
    ctx->pfd [1].fd = ipc_listener;
    ctx->pfd [1].events = POLLIN;
    ctx->pfd_size = 2;
    
    /*  Run the daemon in a dedicated thread. */
    nn_thread_init (&ctx->thread, nn_tcpmuxd_routine, ctx);

    return 0;
}

/*  Main body of the daemon. */
static void nn_tcpmuxd_routine (void *arg)
{
    int rc;
    struct nn_tcpmuxd_ctx *ctx;
    int conn;
    int pos;
    char service [256];
    struct nn_tcpmuxd_conn *tc;
    size_t sz;
    ssize_t ssz;
    int i;
    struct nn_list_item *it;
    unsigned char buf [2];
    struct timeval tv;

    ctx = (struct nn_tcpmuxd_ctx*) arg;

    while (1) {

        /*  Wait for events. */
        rc = poll (ctx->pfd, ctx->pfd_size, -1);
        errno_assert (rc >= 0);
        nn_assert (rc != 0);

        /*  There's an incoming TCP connection. */
        if (ctx->pfd [0].revents & POLLIN) {

            /*  Accept the connection. */
            conn = accept (ctx->tcp_listener, NULL, NULL);
            if (conn < 0 && errno == ECONNABORTED)
                continue;
            errno_assert (conn >= 0);

            /*  Set timeouts to prevent malevolent client blocking the service.
                Note that these options are not supported on Solaris. */
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            rc = setsockopt (conn, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
            errno_assert (rc == 0 || (rc < 0 && errno == ENOPROTOOPT));
            rc = setsockopt (conn, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));
            errno_assert (rc == 0 || (rc < 0 && errno == ENOPROTOOPT));

            /*  Read TCPMUX header. */
            pos = 0;
            while (1) {
                nn_assert (pos < sizeof (service));
                ssz = recv (conn, &service [pos], 1, 0);
                if (ssz < 0 && errno == EAGAIN) {
                    close (conn);
                    continue;
                }
                errno_assert (ssz >= 0);
                nn_assert (ssz == 1);
                service [pos] = tolower (service [pos]);
                if (pos > 0 && service [pos - 1] == 0x0d &&
                      service [pos] == 0x0a)
                    break;
                ++pos;
            }
            service [pos - 1] = 0;
            
            /*  Check whether specified service is listening. */
            for (it = nn_list_begin (&ctx->conns);
                  it != nn_list_end (&ctx->conns);
                  it = nn_list_next (&ctx->conns, it)) {
                tc = nn_cont (it, struct nn_tcpmuxd_conn, item);
                if (strcmp (service, tc->service) == 0)
                    break;
            }

            /* If no one is listening, tear down the connection. */
            if (it == nn_list_end (&ctx->conns)) {
                ssz = send (conn, "-\x0d\x0a", 3, 0);
                if (ssz < 0 && errno == EAGAIN) {
                    close (conn);
                    continue;
                }
                errno_assert (ssz >= 0);
                nn_assert (ssz == 3);
                close (conn);
                continue;
            }

            /*  Send TCPMUX reply. */
            ssz = send (conn, "+\x0d\x0a", 3, 0);
            if (ssz < 0 && errno == EAGAIN) {
                close (conn);
                continue;
            }
            errno_assert (ssz >= 0);
            nn_assert (ssz == 3);

            /*  Pass the file descriptor to the listening process. */
            rc = nn_tcpmuxd_send_fd (tc->fd, conn);
            errno_assert (rc == 0);
        }

        /*  There's an incoming IPC connection. */
        if (ctx->pfd [1].revents & POLLIN) {

            /*  Accept the connection. */
            conn = accept (ctx->ipc_listener, NULL, NULL);
            if (conn < 0 && errno == ECONNABORTED)
                continue;
            errno_assert (conn >= 0);

            /*  Create new connection entry. */
            tc = nn_alloc (sizeof (struct nn_tcpmuxd_conn), "tcpmuxd_conn");
            nn_assert (tc);
            tc->fd = conn;
            nn_list_item_init (&tc->item); 

            /*  Adjust the pollset. We will poll for errors only. */
            ctx->pfd_size++;
            if (ctx->pfd_size > ctx->pfd_capacity) {
                ctx->pfd_capacity *= 2;
                ctx->pfd = nn_realloc (ctx->pfd,
                    sizeof (struct pollfd) * ctx->pfd_capacity);
                alloc_assert (ctx->pfd);
            }
            ctx->pfd [ctx->pfd_size - 1].fd = conn;
            ctx->pfd [ctx->pfd_size - 1].events = 0;
            ctx->pfd [ctx->pfd_size - 1].revents = 0;

            /*  Read the connection header. */
            ssz = recv (conn, buf, 2, 0);
            errno_assert (ssz >= 0);
            nn_assert (ssz == 2);
            sz = nn_gets (buf);
            tc->service = nn_alloc (sz + 1, "tcpmuxd_conn.service");
            nn_assert (tc->service);
            ssz = recv (conn, tc->service, sz, 0);
            errno_assert (ssz >= 0);
            nn_assert (ssz == sz);
            for (i = 0; i != sz; ++i)
                tc->service [i] = tolower (tc->service [i]);
            tc->service [sz] = 0;
            
            /*  Add the entry to the IPC connections list. */
            nn_list_insert (&ctx->conns, &tc->item, nn_list_end (&ctx->conns));
        }

        for (i = 2; i < ctx->pfd_size; ++i) {
            if (ctx->pfd [i].revents & POLLERR ||
                  ctx->pfd [i].revents & POLLHUP) {
                nn_tcpmuxd_disconnect (ctx, i);
                i--;
            }
        }
    }
}

/*  Tear down the IPC connection with index i in the pollset. */
static void nn_tcpmuxd_disconnect (struct nn_tcpmuxd_ctx *ctx, int i)
{
    int fd;
    struct nn_list_item *it;
    struct nn_tcpmuxd_conn *conn;

    fd = ctx->pfd [i].fd;

    /*  Remove the descriptor from the pollset. */
    if (ctx->pfd_size > 3)
        ctx->pfd [i] = ctx->pfd [ctx->pfd_size - 1];
    ctx->pfd_size--;

    /*  Remove the connection entry. */
    for (it = nn_list_begin (&ctx->conns);
          it != nn_list_end (&ctx->conns);
          it = nn_list_next (&ctx->conns, it)) {
        conn = nn_cont (it, struct nn_tcpmuxd_conn, item);
        if (conn->fd == fd) {
            nn_list_erase (&ctx->conns, it);
            nn_free (conn->service);
            nn_free (conn);
            break;
        }
    }
}

/*  Send file descriptor fd to IPC socket s. */
static int nn_tcpmuxd_send_fd (int s, int fd)
{
    int rc;
    struct iovec iov;
    char c = 0;
    struct msghdr msg;
    char control [sizeof (struct cmsghdr) + 10];
#if defined NN_HAVE_MSG_CONTROL
    struct cmsghdr *cmsg;
#endif

    /*  Compose the message. We'll send one byte long dummy message
        accompanied with the fd.*/
    iov.iov_base = &c;
    iov.iov_len = 1;
    memset (&msg, 0, sizeof (msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    /*  Attach the file descriptor to the message. */
#if defined NN_HAVE_MSG_CONTROL
    msg.msg_control = control;
    msg.msg_controllen = sizeof (control);
    cmsg = CMSG_FIRSTHDR (&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN (sizeof (fd));
    int *data = (int*) CMSG_DATA (cmsg);
    *data = fd;
    msg.msg_controllen = cmsg->cmsg_len;
#else
    msg.msg_accrights = (caddr_t) &fd;
    msg.msg_accrightslen = sizeof (fd);
#endif

    /*  Pass the file descriptor to the registered process. */
    rc = sendmsg (s, &msg, 0);
    if (rc < 0)
        return -1;
    nn_assert (rc == 1);

    /*  Sending the file descriptor to other process acts as dup().
        Therefore, we have to close the local copy of the file descriptor. */
    nn_closefd (fd);

    return 0;
}

#endif
