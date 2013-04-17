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

#include "usock.h"

#include "../utils/alloc.h"
#include "../utils/cont.h"
#include "../utils/err.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define NN_USOCK_STATE_STARTING 1
#define NN_USOCK_STATE_CONNECTING 2
#define NN_USOCK_STATE_CONNECTED 3
#define NN_USOCK_STATE_ACCEPTING 4

/*  Private functions. */
static void nn_usock_term (struct nn_usock *self);
static int nn_usock_send_raw (struct nn_usock *self, struct msghdr *hdr);
static int nn_usock_recv_raw (struct nn_usock *self, void *buf, size_t *len);
static int nn_usock_geterr (struct nn_usock *self);
static void nn_usock_callback (struct nn_callback *self, void *source,
    int type);

static int nn_usock_init_from_fd (struct nn_usock *self,
    int fd, struct nn_ctx *ctx, struct nn_callback *callback)
{
    int rc;
    int opt;

    /*  Set up the callback pointers. */
    nn_callback_init (&self->in_callback, nn_usock_callback);
    self->out_callback = callback;

    /*  Store the reference to the worker the socket is associated with. */
    self->ctx = ctx;
    self->worker = nn_ctx_choose_worker (ctx);

    /*  Store the file descriptor of the underlying socket. */
    self->s = fd;

    /* Setting FD_CLOEXEC option immediately after socket creation is the
        second best option after using SOCK_CLOEXEC. There is a race condition
        here (if process is forked between socket creation and setting
        the option) but the problem is pretty unlikely to happen. */
#if defined FD_CLOEXEC
    rc = fcntl (self->s, F_SETFD, FD_CLOEXEC);
#if defined NN_HAVE_OSX
    errno_assert (rc != -1 || errno == EINVAL);
#else
    errno_assert (rc != -1);
#endif
#endif

    /* If applicable, prevent SIGPIPE signal when writing to the connection
        already closed by the peer. */
#ifdef SO_NOSIGPIPE
    opt = 1;
    rc = setsockopt (self, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof (opt));
#if defined NN_HAVE_OSX
    errno_assert (rc == 0 || errno == EINVAL);
#else
    errno_assert (rc == 0);
#endif
#endif

    /* Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the callbackhronous mode. */
    opt = fcntl (self->s, F_GETFL, 0);
    if (opt == -1)
        opt = 0;
    if (!(opt & O_NONBLOCK)) {
        rc = fcntl (self->s, F_SETFL, opt | O_NONBLOCK);
#if defined NN_HAVE_OSX
        errno_assert (rc != -1 || errno == EINVAL);
#else
        errno_assert (rc != -1);
#endif
    }

    self->state = NN_USOCK_STATE_STARTING;

    self->in.buf = NULL;
    self->in.len = 0;
    self->in.batch = NULL;
    self->in.batch_len = 0;
    self->in.batch_pos = 0;

    memset (&self->out.hdr, 0, sizeof (struct msghdr));

    /*  Initialise sources of callbacks. */
    nn_worker_fd_init (&self->wfd, &self->in_callback);
    nn_worker_task_init (&self->connect_task, &self->in_callback);
    nn_worker_task_init (&self->connected_task, &self->in_callback);
    nn_worker_task_init (&self->accept_task, &self->in_callback);
    nn_worker_task_init (&self->send_task, &self->in_callback);
    nn_worker_task_init (&self->recv_task, &self->in_callback);
    nn_worker_task_init (&self->close_task, &self->in_callback);

    /*  We are not accepting a connection at the moment. */
    self->newsock = NULL;
    self->newcallback = NULL;

    return 0;
}

int nn_usock_init (struct nn_usock *self, int domain, int type, int protocol,
    struct nn_ctx *ctx, struct nn_callback *callback)
{
    int s;

    /*  If the operating system allows to directly open the socket with CLOEXEC
        flag, do so. That way there are no race conditions. */
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif

    /* Open the underlying socket. */
    s = socket (domain, type, protocol);
    if (s < 0)
       return -errno;

    return nn_usock_init_from_fd (self, s, ctx, callback);
}

void nn_usock_close (struct nn_usock *self)
{
    /*  Ask socket to close asynchronously. */
    nn_worker_execute (self->worker, &self->close_task);
}

static void nn_usock_term (struct nn_usock *self)
{
    int rc;

    if (self->in.batch)
        nn_free (self->in.batch);

    nn_worker_task_term (&self->close_task);
    nn_worker_task_term (&self->recv_task);
    nn_worker_task_term (&self->send_task);
    nn_worker_task_term (&self->accept_task);
    nn_worker_task_term (&self->connected_task);
    nn_worker_task_term (&self->connect_task);
    nn_worker_fd_term (&self->wfd);

    rc = close (self->s);
    errno_assert (rc == 0);

    nn_callback_term (&self->in_callback);
}

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen)
{
    int rc;

    /*  EINVAL errors are ignored on OSX platform. The reason for that is buggy
        OSX behaviour where setsockopt returns EINVAL if the peer have already
        disconnected. Thus, nn_usock_setsockopt() can succeed on OSX even though
        the option value was invalid, but the peer have already closed the
        connection. This behaviour should be relatively harmless. */
    rc = setsockopt (self->s, level, optname, optval, (socklen_t) optlen);
#if defined NN_HAVE_OSX
    if (nn_slow (rc != 0 && errno != EINVAL))
        return -errno;
#else
    if (nn_slow (rc != 0))
        return -errno;
#endif

    return 0;
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;

    rc = bind (self->s, addr, (socklen_t) addrlen);
    if (nn_slow (rc != 0))
        return -errno;

    return 0;
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    int rc;

    /*  Start listening for incoming connections. */
    rc = listen (self->s, backlog);
    if (nn_slow (rc != 0))
        return -errno;

    return 0;
}

void nn_usock_accept (struct nn_usock *self, struct nn_usock *newsock,
    struct nn_callback *newcallback)
{
    int s;

    /*  If newsock is not NULL, other accept is in progress. That should never
        happen. */
    nn_assert (!self->newsock);

#if NN_HAVE_ACCEPT4
    s = accept4 (self->s, NULL, NULL, SOCK_CLOEXEC);
#else
    s = accept (self->s, NULL, NULL);
#endif

    /*  Immediate success. */
    if (nn_fast (s >= 0)) {
        nn_usock_init_from_fd (newsock, s, self->ctx, newcallback);
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_ACCEPTED);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Unexpected failure. */
    if (nn_slow (errno != EAGAIN && errno != EWOULDBLOCK &&
          errno != ECONNABORTED)) {
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_ERROR);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Ask the worker thread to wait for the new connection. */
    self->newsock = newsock;
    self->newcallback = newcallback;
    nn_worker_execute (self->worker, &self->accept_task);    
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;

    /* Do the connect itself. */
    rc = connect (self->s, addr, (socklen_t) addrlen);

    /* Immediate success. */
    if (nn_fast (rc == 0)) {

        /*  Ask worker thread to start polling on the socket. */
        nn_worker_execute (self->worker, &self->connected_task);

        /*  Notify the user that the connection is established. */
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_CONNECTED);
        nn_ctx_leave (self->ctx);
        return;
    }

    /* Return unexpected errors to the caller. Notify the user about it. */
    if (nn_slow (errno != EINPROGRESS)) {
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_ERROR);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Ask worker thread to start waiting for connection establishment. */
    nn_worker_execute (self->worker, &self->connect_task);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iovec *iov,
    int iovcnt)
{
    int rc;
    int i;
    int out;

    /*  Copy the iovecs to the socket. */
    nn_assert (iovcnt <= NN_USOCK_MAX_IOVCNT);
    self->out.hdr.msg_iov = self->out.iov;
    out = 0;
    for (i = 0; i != iovcnt; ++i) {
        if (iov [i].iov_len == 0)
            continue;
        self->out.iov [out].iov_base = iov [i].iov_base;
        self->out.iov [out].iov_len = iov [i].iov_len;
        out++;
    }
    self->out.hdr.msg_iovlen = out; 
    
    /*  Try to send the data immediately. */
    rc = nn_usock_send_raw (self, &self->out.hdr);

    /*  Success. */
    if (nn_fast (rc == 0)) {
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_SENT);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Errors. */
    if (nn_slow (rc != -EAGAIN)) {
        errnum_assert (rc == -ECONNRESET, -rc);
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_ERROR);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Ask the worker thread to send the remaining data. */
    nn_worker_execute (self->worker, &self->send_task);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    int rc;
    size_t nbytes;

    /*  Try to receive the data immediately. */
    nbytes = len;
    rc = nn_usock_recv_raw (self, buf, &nbytes);
    if (nn_slow (rc < 0)) {
        errnum_assert (rc == -ECONNRESET, -rc);
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_ERROR);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  Success. */
    if (nn_fast (nbytes == len)) {
        nn_ctx_enter (self->ctx);
        self->out_callback->fn (self->out_callback, self, NN_USOCK_RECEIVED);
        nn_ctx_leave (self->ctx);
        return;
    }

    /*  There are still data to receive in the background. */
    self->in.buf = ((uint8_t*) buf) + nbytes;
    self->in.len = len - nbytes;

    /*  Ask the worker thread to receive the remaining data. */
    nn_worker_execute (self->worker, &self->recv_task);
}

static void nn_usock_callback (struct nn_callback *self, void *source, int type)
{
    int rc;
    struct nn_usock *usock;
    int s;
    size_t sz;
    struct nn_callback *out_callback;

    usock = nn_cont (self, struct nn_usock, in_callback);

    /*  Close event is processed in the same way not depending on the state
        the usock is in. */
    if (source == &usock->close_task) {
        nn_worker_rm_fd (usock->worker, &usock->wfd);
        out_callback = usock->out_callback;
        nn_usock_term (usock);
        nn_ctx_enter (usock->ctx);
        out_callback->fn (usock->out_callback, usock, NN_USOCK_CLOSED);
        nn_ctx_leave (usock->ctx);
        return;
    }

    switch (usock->state) {

/******************************************************************************/
/*  STARTING                                                                  */
/******************************************************************************/
    case NN_USOCK_STATE_STARTING:
        if (source == &usock->connected_task) {
            nn_assert (type == NN_WORKER_TASK_EXECUTE);
            nn_worker_add_fd (usock->worker, usock->s, &usock->wfd);
            usock->state = NN_USOCK_STATE_CONNECTED;
            return;
        }
        if (source == &usock->connect_task) {
            nn_assert (type == NN_WORKER_TASK_EXECUTE);
            nn_worker_add_fd (usock->worker, usock->s, &usock->wfd);
            nn_worker_set_out (usock->worker, &usock->wfd);
            usock->state = NN_USOCK_STATE_CONNECTING;
            return;
        }
        if (source == &usock->accept_task) {
            nn_assert (type == NN_WORKER_TASK_EXECUTE);
            nn_worker_add_fd (usock->worker, usock->s, &usock->wfd);
            nn_worker_set_in (usock->worker, &usock->wfd);
            usock->state = NN_USOCK_STATE_ACCEPTING;
            return;
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTING                                                                */
/******************************************************************************/ 
    case NN_USOCK_STATE_CONNECTING:
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_OUT:
                nn_worker_reset_out (usock->worker, &usock->wfd);
                usock->state = NN_USOCK_STATE_CONNECTED;
                nn_ctx_enter (usock->ctx);
                usock->out_callback->fn (usock->out_callback, usock,
                    NN_USOCK_CONNECTED);
                nn_ctx_leave (usock->ctx);
                return;
            case NN_WORKER_FD_ERR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACCEPTING                                                                 */
/******************************************************************************/ 
    case NN_USOCK_STATE_ACCEPTING:
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_IN:
                nn_assert (usock->newsock);
#if NN_HAVE_ACCEPT4
                s = accept4 (usock->s, NULL, NULL, SOCK_CLOEXEC);
#else
                s = accept (usock->s, NULL, NULL);
#endif
                /*  ECONNABORTED is an valid error. If it happens do nothing
                    and wait for next incoming connection to accept. */
                if (s < 0) {
                    if (errno == ECONNABORTED)
                        return;
                    errno_assert (0);
                }

                nn_usock_init_from_fd (usock->newsock, s, usock->ctx,
                    usock->newcallback);
                nn_worker_add_fd (usock->newsock->worker, usock->newsock->s,
                    &usock->newsock->wfd);
                nn_ctx_enter (usock->ctx);
                usock->out_callback->fn (usock->out_callback, usock,
                    NN_USOCK_ACCEPTED);
                nn_ctx_leave (usock->ctx);
                usock->newsock = NULL;
                usock->newcallback = NULL;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTED                                                                 */
/******************************************************************************/ 
    case NN_USOCK_STATE_CONNECTED:
        if (source == &usock->send_task) {
            nn_assert (type == NN_WORKER_TASK_EXECUTE);
            nn_worker_set_out (usock->worker, &usock->wfd);
            return;
        }
        if (source == &usock->recv_task) {
            nn_assert (type == NN_WORKER_TASK_EXECUTE);
            nn_worker_set_in (usock->worker, &usock->wfd);
            return;
        }
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_IN:
                sz = usock->in.len;
                rc = nn_usock_recv_raw (usock, usock->in.buf, &sz);
                if (nn_fast (rc == 0)) {
                    usock->in.len -= sz;
                    if (!usock->in.len) {
                        nn_worker_reset_in (usock->worker, &usock->wfd);
                        nn_ctx_enter (usock->ctx);
                        usock->out_callback->fn (usock->out_callback, usock,
                            NN_USOCK_RECEIVED);
                        nn_ctx_leave (usock->ctx);
                    }
                    return;
                }
                errnum_assert (rc == -ECONNRESET, -rc);
                nn_ctx_enter (usock->ctx);
                usock->out_callback->fn (usock->out_callback, usock,
                    NN_USOCK_ERROR);
                nn_ctx_leave (usock->ctx);
                return;
            case NN_WORKER_FD_OUT:
                rc = nn_usock_send_raw (usock, &usock->out.hdr);
                if (nn_fast (rc == 0)) {
                    nn_worker_reset_out (usock->worker, &usock->wfd);
                    nn_ctx_enter (usock->ctx);
                    usock->out_callback->fn (usock->out_callback, usock,
                        NN_USOCK_SENT);
                    nn_ctx_leave (usock->ctx);
                    return;
                }
                if (nn_fast (rc == -EAGAIN))
                    return;
                errnum_assert (rc == -ECONNRESET, -rc);
                nn_ctx_enter (usock->ctx);
                usock->out_callback->fn (usock->out_callback, usock,
                    NN_USOCK_ERROR);
                nn_ctx_leave (usock->ctx);
                return;
            case NN_WORKER_FD_ERR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  Invalid state                                                             */
/******************************************************************************/ 
    default:
        nn_assert (0);
    }
}

static int nn_usock_send_raw (struct nn_usock *self, struct msghdr *hdr)
{
    ssize_t nbytes;

    /*  Try to send the data. */
#if defined MSG_NOSIGNAL
    nbytes = sendmsg (self->s, hdr, MSG_NOSIGNAL);
#else
    nbytes = sendmsg (self->s, hdr, 0);
#endif

    /*  Handle errors. */
    if (nn_slow (nbytes < 0)) {
        if (nn_fast (errno == EAGAIN || errno == EWOULDBLOCK))
            nbytes = 0;
        else {

            /*  If the connection fails, return ECONNRESET. */
            errno_assert (errno == ECONNRESET || errno == ETIMEDOUT ||
                errno == EPIPE);
            return -ECONNRESET;
        }
    }

    /*  Some bytes were sent. Adjust the iovecs accordingly. */
    while (nbytes) {
        if (nbytes >= hdr->msg_iov->iov_len) {
            --hdr->msg_iovlen;
            if (!hdr->msg_iovlen) {
                nn_assert (nbytes == hdr->msg_iov->iov_len);
                return 0;
            }
            nbytes -= hdr->msg_iov->iov_len;
            ++hdr->msg_iov;
        }
        else {
            hdr->msg_iov->iov_base += nbytes;
            hdr->msg_iov->iov_len -= nbytes;
            return -EAGAIN;
        }
    }

    if (hdr->msg_iovlen > 0)
        return -EAGAIN;

    return 0;
}

static int nn_usock_recv_raw (struct nn_usock *self, void *buf, size_t *len)
{
    size_t sz;
    size_t length;
    ssize_t nbytes;

    /*  If batch buffer doesn't exist, allocate it. The point of delayed
        deallocation to allow non-receiving sockets, such as TCP listening
        sockets, to do without the batch buffer. */
    if (nn_slow (!self->in.batch)) {
        self->in.batch = nn_alloc (NN_USOCK_BATCH_SIZE, "AIO batch buffer");
        alloc_assert (self->in.batch);
    }

    /*  Try to satisfy the recv request by data from the batch buffer. */
    length = *len;
    sz = self->in.batch_len - self->in.batch_pos;
    if (sz) {
        if (sz > length)
            sz = length;
        memcpy (buf, self->in.batch + self->in.batch_pos, sz);
        self->in.batch_pos += sz;
        buf = ((char*) buf) + sz;
        length -= sz;
        if (!length)
            return 0;
    }

    /*  If recv request is greater than the batch buffer, get the data directly
        into the place. Otherwise, read data to the batch buffer. */
    if (length > NN_USOCK_BATCH_SIZE)
        nbytes = recv (self->s, buf, length, 0);
    else 
        nbytes = recv (self->s, self->in.batch, NN_USOCK_BATCH_SIZE, 0);

    /*  Handle any possible errors. */
    if (nn_slow (nbytes <= 0)) {

        if (nn_slow (nbytes == 0))
            return -ECONNRESET; 

        /*  Zero bytes received. */
        if (nn_fast (errno == EAGAIN || errno == EWOULDBLOCK))
            nbytes = 0;
        else {

            /*  If the peer closes the connection, return ECONNRESET. */
            errno_assert (errno == ECONNRESET || errno == ENOTCONN ||
                errno == ECONNREFUSED || errno == ETIMEDOUT ||
                errno == EHOSTUNREACH);
            return -ECONNRESET;
        }
    }

    /*  If the data were received directly into the place we can return
        straight away. */
    if (length > NN_USOCK_BATCH_SIZE) {
        length -= nbytes;
        *len -= length;
        return 0;
    }

    /*  New data were read to the batch buffer. Copy the requested amount of it
        to the user-supplied buffer. */
    self->in.batch_len = nbytes;
    self->in.batch_pos = 0;
    if (nbytes) {
        sz = nbytes > length ? length : nbytes;
        memcpy (buf, self->in.batch, sz);
        length -= sz;
        self->in.batch_pos += sz;
    }

    *len -= length;
    return 0;
}

static int nn_usock_geterr (struct nn_usock *self)
{
    int rc;
    int opt;
#if defined NN_HAVE_HPUX
    int optsz;
#else
    socklen_t optsz;
#endif

    opt = 0;
    optsz = sizeof (opt);
    rc = getsockopt (self->s, SOL_SOCKET, SO_ERROR, &opt, &optsz);

    /*  The following should handle both Solaris and UNIXes derived from BSD. */
    if (rc == -1)
        return errno;
    errno_assert (rc == 0);
    nn_assert (optsz == sizeof (opt));
    return opt;
}
