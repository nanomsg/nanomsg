/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#include "aio.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/fast.h"
#include "../utils/alloc.h"

#define _GNU_SOURCE
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

/*  Private functions. */
static void nn_cp_worker (void *arg);
static void nn_usock_tune (struct nn_usock *self, int sndbuf, int rcvbuf);
static int nn_usock_send_raw (struct nn_usock *self, struct msghdr *hdr);
static int nn_usock_recv_raw (struct nn_usock *self, void *buf, size_t *len);
static int nn_usock_geterr (struct nn_usock *self);
static void nn_uscok_term (struct nn_usock *self);

void nn_timer_init (struct nn_timer *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    nn_timerset_hndl_init (&self->hndl);
}

void nn_timer_term (struct nn_timer *self)
{
    nn_timer_stop (self);
    nn_timerset_hndl_term (&self->hndl);
}

void nn_timer_start (struct nn_timer *self, int timeout)
{
    int rc;

    /*  If the timer is active, cancel it first. */
    if (nn_timerset_hndl_isactive (&self->hndl))
        nn_timer_stop (self);

    rc = nn_timerset_add (&self->cp->timeout, timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);

    if (rc == 1 && !nn_thread_current (&self->cp->worker))
        nn_efd_signal (&self->cp->efd);
}

void nn_timer_stop (struct nn_timer *self)
{
    int rc;

    /*  If the timer is not active, do nothing. */
    if (!nn_timerset_hndl_isactive (&self->hndl))
         return;

    rc = nn_timerset_rm (&self->cp->timeout, &self->hndl);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1 && !nn_thread_current (&self->cp->worker))
        nn_efd_signal (&self->cp->efd);
}

void nn_event_init (struct nn_event *self, const struct nn_cp_sink **sink,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->cp = cp;
    nn_queue_item_init (&self->item);
}

void nn_event_term (struct nn_event *self)
{
    nn_queue_item_term (&self->item);
}

void nn_event_signal (struct nn_event *self)
{
    /*  Enqueue the event for later processing. */
    nn_mutex_lock (&self->cp->events_sync);
    nn_queue_push (&self->cp->events, &self->item);
    nn_mutex_unlock (&self->cp->events_sync);
    nn_efd_signal (&self->cp->efd);
}

int nn_usock_init (struct nn_usock *self, const struct nn_cp_sink **sink,
    int domain, int type, int protocol, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    int rc;
#endif

    self->sink = sink;
    self->cp = cp;
    self->in.batch = NULL;
    self->in.batch_len = 0;
    self->in.batch_pos = 0;
    self->in.op = NN_USOCK_INOP_NONE;
    self->out.op = NN_USOCK_OUTOP_NONE;
    nn_queue_item_init (&self->add_hndl.item);
    self->add_hndl.op = NN_USOCK_OP_ADD;
    nn_queue_item_init (&self->rm_hndl.item);
    self->rm_hndl.op = NN_USOCK_OP_RM;
    nn_queue_item_init (&self->in.hndl.item);
    self->in.hndl.op = NN_USOCK_OP_IN;
    nn_queue_item_init (&self->out.hndl.item);
    self->out.hndl.op = NN_USOCK_OP_OUT;
    memset (&self->out.hdr, 0, sizeof (struct msghdr));
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;
    self->protocol = 0;
    self->flags = 0;

    /*  If the operating system allows to directly open the socket with CLOEXEC
        flag, do so. That way there are no race conditions. */
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif

    /*  Open the underlying socket. */
    self->s = socket (domain, type, protocol);
    if (self->s < 0)
       return -errno;

    /*  Setting FD_CLOEXEC option immediately after socket creation is the
        second best option. There is a race condition (if process is forked
        between socket creation and setting the option) but the problem is
        pretty unlikely to happen. */
#if !defined SOCK_CLOEXEC && defined FD_CLOEXEC
    rc = fcntl (self->s, F_SETFD, FD_CLOEXEC);
    errno_assert (rc != -1);
#endif

    nn_usock_tune (self, sndbuf, rcvbuf);

    return 0;
}

const struct nn_cp_sink **nn_usock_setsink (struct nn_usock *self,
    const struct nn_cp_sink **sink)
{
    const struct nn_cp_sink **original;

    original = self->sink;
    self->sink = sink;
    return original;
}

int nn_usock_init_child (struct nn_usock *self, struct nn_usock *parent,
    int s, const struct nn_cp_sink **sink, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
    self->sink = sink;
    self->s = s;
    self->cp = cp;
    self->in.batch = NULL;
    self->in.batch_len = 0;
    self->in.batch_pos = 0;
    self->in.op = NN_USOCK_INOP_NONE;
    self->out.op = NN_USOCK_OUTOP_NONE;
    nn_queue_item_init (&self->add_hndl.item);
    self->add_hndl.op = NN_USOCK_OP_ADD;
    nn_queue_item_init (&self->rm_hndl.item);
    self->rm_hndl.op = NN_USOCK_OP_RM;
    nn_queue_item_init (&self->in.hndl.item);
    self->in.hndl.op = NN_USOCK_OP_IN;
    nn_queue_item_init (&self->out.hndl.item);
    self->out.hndl.op = NN_USOCK_OP_OUT;
    memset (&self->out.hdr, 0, sizeof (struct msghdr));
    self->domain = parent->domain;
    self->type = parent->type;
    self->protocol = parent->protocol;
    self->flags = 0;

    nn_usock_tune (self, sndbuf, rcvbuf);

    /*  Register the new socket with the suplied completion port. 
        If the function is called from the worker thread, modify the pollset
        straight away. Otherwise send an event to the worker thread. */
    self->flags |= NN_USOCK_FLAG_REGISTERED;
    if (nn_thread_current (&self->cp->worker))
        nn_poller_add (&self->cp->poller, self->s, &self->hndl);
    else {
        nn_queue_push (&self->cp->opqueue, &self->add_hndl.item);
        nn_efd_signal (&self->cp->efd);
    }

    return 0;
}

static void nn_usock_tune (struct nn_usock *self, int sndbuf, int rcvbuf)
{
    int rc;
    int opt;
    int flags;
    int only;

    /*  TODO: Currently, EINVAL errors are ignored on OSX platform. The reason
        for that is buggy OSX behaviour where setsockopt returns EINVAL if the
        peer have already disconnected. In the future we should return the
        error to the caller and let it handle it in a decent way. */

    /*  Set the size of tx and rc buffers. */
    if (sndbuf >= 0) {
        rc = setsockopt (self->s, SOL_SOCKET, SO_SNDBUF,
            &sndbuf, sizeof (sndbuf));
#if defined NN_HAVE_OSX
        errno_assert (rc == 0 || errno == EINVAL);
#else
        errno_assert (rc == 0);
#endif
    }
    if (rcvbuf >= 0) {
        rc = setsockopt (self->s, SOL_SOCKET, SO_RCVBUF,
            &rcvbuf, sizeof (rcvbuf));
#if defined NN_HAVE_OSX
        errno_assert (rc == 0 || errno == EINVAL);
#else
        errno_assert (rc == 0);
#endif
    }

    /*  If applicable, prevent SIGPIPE signal when writing to the connection
        already closed by the peer. */
#ifdef SO_NOSIGPIPE
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_NOSIGPIPE,
        &opt, sizeof (opt));
#if defined NN_HAVE_OSX
    errno_assert (rc == 0 || errno == EINVAL);
#else
    errno_assert (rc == 0);
#endif
#endif

    /*  Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the asynchronous mode. */
	flags = fcntl (self->s, F_GETFL, 0);
	if (flags == -1)
        flags = 0;
	rc = fcntl (self->s, F_SETFL, flags | O_NONBLOCK);
#if defined NN_HAVE_OSX
    errno_assert (rc != -1 || errno == EINVAL);
#else
    errno_assert (rc != -1);
#endif

    /*  On TCP sockets switch off the Nagle's algorithm to get
        the best possible latency. */
    if ((self->domain == AF_INET || self->domain == AF_INET6) &&
          self->type == SOCK_STREAM) {
        opt = 1;
        rc = setsockopt (self->s, IPPROTO_TCP, TCP_NODELAY,
            (const char*) &opt, sizeof (opt));
#if defined NN_HAVE_OSX
        errno_assert (rc == 0 || errno == EINVAL);
#else
        errno_assert (rc == 0);
#endif
    }

    /*  If applicable, disable delayed acknowledgments to improve latency. */
#if defined TCP_NODELACK
    opt = 1;
    rc = setsockopt (self->s, IPPROTO_TCP, TCP_NODELACK, &opt, sizeof (opt));
#if defined NN_HAVE_OSX
    errno_assert (rc == 0 || errno == EINVAL);
#else
    errno_assert (rc == 0);
#endif
#endif

    /*  On some operating systems IPv4 mapping for IPv6 sockets is disabled
        by default. In such case, switch it on. */
#if defined IPV6_V6ONLY
    if (self->domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->s, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char*) &only, sizeof (only));
#if defined NN_HAVE_OSX
        errno_assert (rc == 0 || errno == EINVAL);
#else
        errno_assert (rc == 0);
#endif
    }
#endif
}

static void nn_usock_term (struct nn_usock *self)
{
    int rc;

    if (self->in.batch)
        nn_free (self->in.batch);
    rc = close (self->s);
    errno_assert (rc == 0);
    nn_queue_item_term (&self->add_hndl.item);
    nn_queue_item_term (&self->rm_hndl.item);
    nn_queue_item_term (&self->in.hndl.item);
    nn_queue_item_term (&self->out.hndl.item);
    nn_assert ((*self->sink)->closed);
    (*self->sink)->closed (self->sink, self);
}

int nn_cp_init (struct nn_cp *self)
{
    int rc;

    rc = nn_efd_init (&self->efd);
    if (nn_slow (rc < 0))
        return rc;
    rc = nn_poller_init (&self->poller);
    if (nn_slow (rc < 0)) {
        nn_efd_term (&self->efd);
        return rc;
    }
    nn_mutex_init (&self->sync);
    nn_timerset_init (&self->timeout);
    nn_queue_init (&self->opqueue);
    nn_mutex_init (&self->events_sync);
    nn_queue_init (&self->events);

    /*  Make poller listen on the internal efd object. */
    nn_poller_add (&self->poller, nn_efd_getfd (&self->efd),
        &self->efd_hndl);
    nn_poller_set_in (&self->poller, &self->efd_hndl);

    /*  Launch the worker thread. */
    self->stop = 0;
    nn_thread_init (&self->worker, nn_cp_worker, self);

    return 0;
}

void nn_cp_term (struct nn_cp *self)
{
    /*  Ask worker thread to terminate. */
    nn_mutex_lock (&self->sync);
    self->stop = 1;
    nn_efd_signal (&self->efd);
    nn_mutex_unlock (&self->sync);

    /*  Wait till it terminates. */
    nn_thread_term (&self->worker);

    /*  Remove the remaining internal fd from the poller. */
    nn_poller_rm (&self->poller, &self->efd_hndl);

    /*  Deallocate the resources. */
    nn_queue_term (&self->opqueue);
    nn_queue_term (&self->events);
    nn_mutex_term (&self->events_sync);
    nn_poller_term (&self->poller);
    nn_efd_term (&self->efd);
    nn_timerset_term (&self->timeout);
    nn_mutex_term (&self->sync);
}

void nn_cp_lock (struct nn_cp *self)
{
    nn_mutex_lock (&self->sync);
}

void nn_cp_unlock (struct nn_cp *self)
{
    nn_mutex_unlock (&self->sync);
}

static void nn_cp_worker (void *arg)
{
    int rc;
    struct nn_cp *self;
    int timeout;
    struct nn_queue_item *qit;
    struct nn_cp_op_hndl *ophndl;
    struct nn_timerset_hndl *tohndl;
    struct nn_timer *timer;
    int op;
    struct nn_poller_hndl *phndl;
    struct nn_queue_item *it;
    struct nn_event *event;
    struct nn_usock *usock;
    size_t sz;
    int newsock;

    self = (struct nn_cp*) arg;

    nn_mutex_lock (&self->sync);

    while (1) {

        /*  Compute the time interval till next timer expiration. */
        timeout = nn_timerset_timeout (&self->timeout);

        /*  Wait for new events and/or timeouts. */
        nn_mutex_unlock (&self->sync);
again:
        rc = nn_poller_wait (&self->poller, timeout);
if (rc == -EINTR) goto again;
        errnum_assert (rc == 0, -rc);
        nn_mutex_lock (&self->sync);

        /*  Termination of the worker thread. */
        if (self->stop) {
            nn_mutex_unlock (&self->sync);
            break;
        }

        /*  Process the events in the opqueue. */
        while (1) {

            qit = nn_queue_pop (&self->opqueue);
            ophndl = nn_cont (qit, struct nn_cp_op_hndl, item);
            if (!ophndl)
                break;

            switch (ophndl->op) {
            case NN_USOCK_OP_IN:
                usock = nn_cont (ophndl, struct nn_usock, in.hndl);
                nn_poller_set_in (&self->poller, &usock->hndl);
                break;
            case NN_USOCK_OP_OUT:
                usock = nn_cont (ophndl, struct nn_usock, out.hndl);
                nn_poller_set_out (&self->poller, &usock->hndl);
                break;
            case NN_USOCK_OP_ADD:
                usock = nn_cont (ophndl, struct nn_usock, add_hndl);
                nn_poller_add (&self->poller, usock->s, &usock->hndl);
                break;
            case NN_USOCK_OP_RM:
                usock = nn_cont (ophndl, struct nn_usock, rm_hndl);
                nn_poller_rm (&self->poller, &usock->hndl);
                nn_usock_term (usock);
                break;
            default:
                nn_assert (0);
            }
        }

        /*  Process any expired timers. */
        while (1) {
            rc = nn_timerset_event (&self->timeout, &tohndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  Fire the timeout event. */
            timer = nn_cont (tohndl, struct nn_timer, hndl);
            nn_assert ((*timer->sink)->timeout);
            (*timer->sink)->timeout (timer->sink, timer);
        }

        /*  Process any events from the poller. */
        while (1) {
            rc = nn_poller_event (&self->poller, &op, &phndl);
            if (rc == -EAGAIN)
                break;
            errnum_assert (rc == 0, -rc);

            /*  The events delivered through the internal efd object require
                no action in response. Their sole intent is to interrupt the
                waiting. */
            if (phndl == &self->efd_hndl) {
                nn_assert (op == NN_POLLER_IN);
                nn_efd_unsignal (&self->efd);
                continue;
            }

            /*  Process the I/O event. */
            usock = nn_cont (phndl, struct nn_usock, hndl);
            switch (op) {
            case NN_POLLER_IN:
                switch (usock->in.op) {
                case NN_USOCK_INOP_RECV:
                    sz = usock->in.len;
                    rc = nn_usock_recv_raw (usock, usock->in.buf, &sz);
                    if (rc < 0)
                        goto err;
                    usock->in.len -= sz;
                    if (!usock->in.len) {
                        usock->in.op = NN_USOCK_INOP_NONE;
                        nn_poller_reset_in (&self->poller, &usock->hndl);
                        nn_assert ((*usock->sink)->received);
                        (*usock->sink)->received (usock->sink, usock);
                    }
                    break;                    
                case NN_USOCK_INOP_ACCEPT:
                    newsock = accept (usock->s, NULL, NULL);
                    if (newsock == -1) {

                        /*  The following are recoverable errors when accepting
                            a new connection. We can continue waiting for new
                            connection without even notifying the user. */
                        if (errno == ECONNABORTED ||
                              errno == EPROTO || errno == ENOBUFS ||
                              errno == ENOMEM || errno == EMFILE ||
                              errno == ENFILE)
                            break;

                        usock->in.op = NN_USOCK_INOP_NONE;
                        nn_poller_reset_in (&self->poller, &usock->hndl);
                        rc = -errno;
                        goto err;
                    }
                    usock->in.op = NN_USOCK_INOP_NONE;
                    nn_poller_reset_in (&self->poller, &usock->hndl);
                    nn_assert ((*usock->sink)->accepted);
                    (*usock->sink)->accepted (usock->sink, usock, newsock);
                    break;
                case NN_USOCK_INOP_NONE:
                    /*  When non-blocking connect fails both OUT and IN
                        are signaled, which means we can end up here. */
                    break;
                default:
                    nn_assert (0);
                }
                break;
            case NN_POLLER_OUT:
                switch (usock->out.op) {
                case NN_USOCK_OUTOP_SEND:
                    rc = nn_usock_send_raw (usock, &usock->out.hdr);
                    if (nn_fast (rc == 0)) {
                        usock->out.op = NN_USOCK_OUTOP_NONE;
                        nn_poller_reset_out (&self->poller, &usock->hndl);
                        nn_assert ((*usock->sink)->sent);
                        (*usock->sink)->sent (usock->sink, usock);
                        break;
                    }
                    if (nn_fast (rc == -EAGAIN))
                        break;
                    goto err;
                case NN_USOCK_OUTOP_CONNECT:
                    usock->out.op = NN_USOCK_OUTOP_NONE;
                    nn_poller_reset_out (&self->poller, &usock->hndl);
                    rc = nn_usock_geterr (usock);
                    if (rc != 0)
                        goto err;
                    nn_assert ((*usock->sink)->connected);
                    (*usock->sink)->connected (usock->sink, usock);
                    break;
                default:
                    nn_assert (0);
                }
                break;
            case NN_POLLER_ERR:
                rc = nn_usock_geterr (usock);
err:
                nn_assert ((*usock->sink)->err);
                (*usock->sink)->err (usock->sink, usock, rc);
                break;
            default:
                nn_assert (0);
            }
        }

        /*  Process any external events. */
        nn_mutex_lock (&self->events_sync);
        while (1) {
            it = nn_queue_pop (&self->events);
            if (!it)
                break;
            event = nn_cont (it ,struct nn_event, item);
            nn_assert ((*event->sink)->event);
            (*event->sink)->event (event->sink, event);
        }
        nn_mutex_unlock (&self->events_sync);
    }
}

void nn_usock_close (struct nn_usock *self)
{
    int rc;

    /*  If the underlying fd was not yet reigstered with the poller we can
        close the usock straight away. */
    if (!(self->flags & NN_USOCK_FLAG_REGISTERED)) {
        nn_usock_term (self);
        return;
    }

    /*  In the worker thread we can remove the fd from the pollset
        in a synchronous way. */
    if (nn_thread_current (&self->cp->worker)) {
        nn_poller_rm (&self->cp->poller, &self->hndl);
        nn_usock_term (self);
        return;
    }

    /*  Start asynchronous closing of the underlying socket. */
    nn_queue_push (&self->cp->opqueue, &self->rm_hndl.item);
    nn_efd_signal (&self->cp->efd);
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen)
{
    int rc;
    int opt;

    /*  To allow for rapid restart of SP services, allow new bind to succeed
        immediately after previous instance of the process failed, skipping the
        grace period. */
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt));
    errno_assert (rc == 0);

    /*  Bind the local end of the connections. */
    rc = bind (self->s, addr, addrlen);
    if (nn_slow (rc < 0))
       return -errno;

    return 0;
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    int rc;

    /*  Start waiting for incoming connections. */
    rc = listen (self->s, backlog);
    if (nn_slow (rc < 0))
       return -errno;

    /*  If the function is called from the worker thread, modify the pollset
        straight away. Otherwise send an event to the worker thread. */
    if (nn_thread_current (&self->cp->worker))
        nn_poller_add (&self->cp->poller, self->s, &self->hndl);
    else {
        nn_queue_push (&self->cp->opqueue, &self->add_hndl.item);
        nn_efd_signal (&self->cp->efd);
    }

    return 0;
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    nn_socklen addrlen)
{
    int rc;

    /*  Make sure that there's no outbound operation already in progress. */
    nn_assert (self->out.op == NN_USOCK_OUTOP_NONE);

    /*  Do the connect itself. */
    rc = connect (self->s, addr, addrlen);

    /*  Immediate success. */
    if (nn_fast (rc == 0)) {
        self->flags |= NN_USOCK_FLAG_REGISTERED;
        if (nn_thread_current (&self->cp->worker)) {
            nn_poller_add (&self->cp->poller, self->s, &self->hndl);
        }
        else {
            nn_queue_push (&self->cp->opqueue, &self->add_hndl.item);
            nn_efd_signal (&self->cp->efd);
        }
        nn_assert ((*self->sink)->connected);
        (*self->sink)->connected (self->sink, self);
        return;
    }

    /*  Adjust the handle. */
    self->out.op = NN_USOCK_OUTOP_CONNECT;

    /*  Return unexpected errors to the caller. */
    if (nn_slow (errno != EINPROGRESS)) {
        nn_assert ((*self->sink)->err);
        (*self->sink)->err (self->sink, self, errno);
        return;
    }

    /*  If we are in the worker thread we can simply start polling for out.
        Otherwise, ask worker thread to start polling for out. */
    self->flags |= NN_USOCK_FLAG_REGISTERED;
    if (nn_thread_current (&self->cp->worker)) {
        nn_poller_add (&self->cp->poller, self->s, &self->hndl);
        nn_poller_set_out (&self->cp->poller, &self->hndl);
    }
    else {
        nn_queue_push (&self->cp->opqueue, &self->add_hndl.item);
        nn_queue_push (&self->cp->opqueue, &self->out.hndl.item);
        nn_efd_signal (&self->cp->efd);
    }
}

void nn_usock_accept (struct nn_usock *self)
{
    /*  Make sure that there's no inbound operation already in progress. */
    nn_assert (self->in.op == NN_USOCK_INOP_NONE);

    /*  Adjust the handle. */
    self->in.op = NN_USOCK_INOP_ACCEPT;

    /*  If we are in the worker thread we can simply start polling for out.
        Otherwise, ask worker thread to start polling for in. */
    if (nn_thread_current (&self->cp->worker))
        nn_poller_set_in (&self->cp->poller, &self->hndl);
    else {
        nn_queue_push (&self->cp->opqueue, &self->in.hndl.item);
        nn_efd_signal (&self->cp->efd);
    }
}

void nn_usock_send (struct nn_usock *self,
    const struct nn_iobuf *iov, int iovcnt)
{
    int rc;
    int i;
    int out;

    /*  Make sure that there's no outbound operation already in progress. */
    nn_assert (self->out.op == NN_USOCK_OUTOP_NONE);

    /*  Copy the iovecs to the socket. */
    nn_assert (iovcnt <= NN_AIO_MAX_IOVCNT);
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
        nn_assert ((*self->sink)->sent);
        (*self->sink)->sent (self->sink, self);
        return;
    }

    /*  Errors. */
    if (nn_slow (rc != -EAGAIN)) {
        errnum_assert (rc == -ECONNRESET, -rc);
        nn_assert ((*self->sink)->err);
        (*self->sink)->err (self->sink, self, -rc);
        return;
    }

    /*  There are still data to send in the background. */ 
    self->out.op = NN_USOCK_OUTOP_SEND;

    /*  If we are in the worker thread we can simply start polling for out.
        Otherwise, ask worker thread to start polling for out. */
    if (nn_thread_current (&self->cp->worker))
        nn_poller_set_out (&self->cp->poller, &self->hndl);
    else {
        nn_queue_push (&self->cp->opqueue, &self->out.hndl.item);
        nn_efd_signal (&self->cp->efd);
    }
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    int rc;
    size_t nbytes;

    /*  Make sure that there's no inbound operation already in progress. */
    nn_assert (self->in.op == NN_USOCK_INOP_NONE);

    /*  Try to receive the data immediately. */
    nbytes = len;
    rc = nn_usock_recv_raw (self, buf, &nbytes);
    if (nn_slow (rc < 0)) {
        errnum_assert (rc == -ECONNRESET, -rc);
        nn_assert ((*self->sink)->err);
        (*self->sink)->err (self->sink, self, -rc);
        return;
    }

    /*  Success. */
    if (nn_fast (nbytes == len)) {
        nn_assert ((*self->sink)->received);
        (*self->sink)->received (self->sink, self);
        return;
    }

    /*  There are still data to receive in the background. */ 
    self->in.op = NN_USOCK_INOP_RECV;
    self->in.buf = ((uint8_t*) buf) + nbytes;
    self->in.len = len - nbytes;

    /*  If we are in the worker thread we can simply start polling for in.
        Otherwise, ask worker thread to start polling for in. */
    if (nn_thread_current (&self->cp->worker))
        nn_poller_set_in (&self->cp->poller, &self->hndl);
    else {
        nn_queue_push (&self->cp->opqueue, &self->in.hndl.item);
        nn_efd_signal (&self->cp->efd);
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
    int err;
#if defined NN_HAVE_HPUX
    int errlen;
#else
    socklen_t errlen;
#endif

    err = 0;
    errlen = sizeof (err);
    rc = getsockopt (self->s, SOL_SOCKET, SO_ERROR, (char*) &err, &errlen);

    /*  On Solaris error is returned via errno. */
    if (rc == -1)
        return errno;

    /*  On other platforms the error is in err. */
    nn_assert (errlen == sizeof (err));
    return err;
}

