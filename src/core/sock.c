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

#include "../protocol.h"
#include "../transport.h"

#include "sock.h"
#include "global.h"
#include "ep.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/fast.h"
#include "../utils/msg.h"

/*  This flag is set, if nn_term() function was already called. All the socket
    function, except for nn_close() should return ETERM error in such case. */
#define NN_SOCK_FLAG_ZOMBIE 1

/*  These bits specify whether individual efds are signalled or not at
    the moment. Storing this information allows us to avoid redundant signalling
    and unsignalling of the efd objects. */
#define NN_SOCK_FLAG_IN 2
#define NN_SOCK_FLAG_OUT 4

/*  Set if nn_close() is already in progress. */
#define NN_SOCK_FLAG_CLOSING 8

/*  Private functions. */
void nn_sock_adjust_events (struct nn_sock *self);
struct nn_optset *nn_sock_optset (struct nn_sock *self, int id);
static int nn_sock_setopt_inner (struct nn_sock *self, int level,
    int option, const void *optval, size_t optvallen);
static int nn_sock_getopt_inner (struct nn_sock *self, int level,
    int option, void *optval, size_t *optvallen);

int nn_sock_init (struct nn_sock *self, struct nn_socktype *socktype)
{
    int rc;
    int i;

    /* Make sure that at least one message direction is supported. */
    nn_assert (!(socktype->flags & NN_SOCKTYPE_FLAG_NOSEND) ||
        !(socktype->flags & NN_SOCKTYPE_FLAG_NORECV));

    /*  Create the AIO context for the SP socket. */
    nn_ctx_init (&self->ctx, nn_global_getpool ());

    /*  Open the NN_SNDFD and NN_RCVFD efds. Do so, only if the socket type
        supports send/recv, as appropriate. */
    if (socktype->flags & NN_SOCKTYPE_FLAG_NOSEND)
        memset (&self->sndfd, 0xcd, sizeof (self->sndfd));
    else {
        rc = nn_efd_init (&self->sndfd);
        if (nn_slow (rc < 0))
            return rc;
    }
    if (socktype->flags & NN_SOCKTYPE_FLAG_NORECV)
        memset (&self->rcvfd, 0xcd, sizeof (self->rcvfd));
    else {
        rc = nn_efd_init (&self->rcvfd);
        if (nn_slow (rc < 0)) {
            if (!(socktype->flags & NN_SOCKTYPE_FLAG_NOSEND))
                nn_efd_term (&self->sndfd);
            return rc;
        }
    }
    memset (&self->termsem, 0xcd, sizeof (self->termsem));
    if (nn_slow (rc < 0)) {
        if (!(socktype->flags & NN_SOCKTYPE_FLAG_NORECV))
            nn_efd_term (&self->rcvfd);
        if (!(socktype->flags & NN_SOCKTYPE_FLAG_NOSEND))
            nn_efd_term (&self->sndfd);
        return rc;
    }

    self->flags = 0;
    nn_clock_init (&self->clock);
    nn_list_init (&self->eps);
    self->eid = 1;

    /*  Default values for NN_SOL_SOCKET options. */
    self->linger = 1000;
    self->sndbuf = 128 * 1024;
    self->rcvbuf = 128 * 1024;
    self->sndtimeo = -1;
    self->rcvtimeo = -1;
    self->reconnect_ivl = 100;
    self->reconnect_ivl_max = 0;
    self->sndprio = 8;

    /*  The transport-specific options are not initialised immediately,
        rather, they are allocated later on when needed. */
    for (i = 0; i != NN_MAX_TRANSPORT; ++i)
        self->optsets [i] = NULL;

    /*  Create the specific socket type itself. */
    rc = socktype->create ((void*) self, &self->sockbase);
    errnum_assert (rc == 0, -rc);
    self->socktype = socktype;

    return 0;
}

void nn_sock_zombify (struct nn_sock *self)
{
    nn_ctx_enter (&self->ctx);
    self->flags |= NN_SOCK_FLAG_ZOMBIE;

    /*  Reset IN and OUT events to unblock any polling function. */
    if (!(self->flags & NN_SOCK_FLAG_CLOSING)) {
        if (!(self->flags & NN_SOCK_FLAG_IN)) {
            self->flags |= NN_SOCK_FLAG_IN;
            if (!(self->socktype->flags & NN_SOCKTYPE_FLAG_NORECV))
                nn_efd_signal (&self->rcvfd);
        }
        if (!(self->flags & NN_SOCK_FLAG_OUT)) {
            self->flags |= NN_SOCK_FLAG_OUT;
            if (!(self->socktype->flags & NN_SOCKTYPE_FLAG_NOSEND))
                nn_efd_signal (&self->sndfd);
        }
    }

    nn_ctx_leave (&self->ctx);
}

int nn_sock_term (struct nn_sock *self)
{
    int rc;
    struct nn_list_item *it;
    struct nn_epbase *ep;
    int i;

    nn_ctx_enter (&self->ctx);

    /*  The call may have been interrupted by a singal and restarted afterwards.
        In such case don't do the following stuff again. */
    if (!(self->flags & NN_SOCK_FLAG_CLOSING)) {

        /*  Mark the socket as being in process of shutting down. */
        self->flags |= NN_SOCK_FLAG_CLOSING;

        /*  Close sndfd and rcvfd. This should make any current select/poll
            using SNDFD and/or RCVFD exit. */
        if (!(self->socktype->flags & NN_SOCKTYPE_FLAG_NORECV)) {
            nn_efd_term (&self->rcvfd);
            memset (&self->rcvfd, 0xcd, sizeof (self->rcvfd));
        }
        if (!(self->socktype->flags & NN_SOCKTYPE_FLAG_NOSEND)) {
            nn_efd_term (&self->sndfd);
            memset (&self->sndfd, 0xcd, sizeof (self->sndfd));
        }

        /*  Create a semaphore to wait on for all endpoint to terminate. */
        nn_sem_init (&self->termsem);

        /*  Ask all the associated endpoints to terminate. Call to nn_ep_close
            can actually deallocate the endpoint, so take care to get pointer
            to the next endpoint before the call. */
        it = nn_list_begin (&self->eps);
        while (it != nn_list_end (&self->eps)) {
            ep = nn_cont (it, struct nn_epbase, item);
            it = nn_list_next (&self->eps, it);
            rc = nn_ep_close ((void*) ep);
            errnum_assert (rc == 0 || rc == -EINPROGRESS, -rc);      
        }
    }

    /*  Shutdown process was already started but some endpoints are still
        alive. Here we are going to wait till they are all closed. */
    if (!nn_list_empty (&self->eps)) {
        nn_ctx_leave (&self->ctx);
        rc = nn_sem_wait (&self->termsem);
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, -rc);
        nn_ctx_enter (&self->ctx);
        nn_assert (nn_list_empty (&self->eps));
    }

    nn_sem_term (&self->termsem);
    nn_list_term (&self->eps);
    nn_clock_term (&self->clock);
    nn_ctx_term (&self->ctx);

    /*  Destroy any optsets associated with the socket. */
    for (i = 0; i != NN_MAX_TRANSPORT; ++i)
        if (self->optsets [i])
            self->optsets [i]->vfptr->destroy (self->optsets [i]);

    self->sockbase->vfptr->destroy (self->sockbase);

    return 0;
}

struct nn_ctx *nn_sock_getctx (struct nn_sock *self)
{
    return &self->ctx;
}

int nn_sock_ispeer (struct nn_sock *self, int socktype)
{
    /*  If the peer implements a different SP protocol it is not a valid peer.
        Checking it here ensures that even if faulty protocol implementation
        allows for cross-protocol communication, it will never happen
        in practice. */
    if ((self->socktype->protocol & 0xfff0) != (socktype  & 0xfff0))
        return 0;

    /*  As long as the peer speaks the same protocol, socket type itself
        decides which socket types are to be accepted. */
    return self->socktype->ispeer (socktype);
}

int nn_sock_setopt (struct nn_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;

    nn_ctx_enter (&self->ctx);
    rc = nn_sock_setopt_inner (self, level, option, optval, optvallen);
    nn_ctx_leave (&self->ctx);

    return rc;
}

static int nn_sock_setopt_inner (struct nn_sock *self, int level,
    int option, const void *optval, size_t optvallen)
{
    int rc;
    struct nn_optset *optset;
    int val;
    int *dst;

    /*  If nn_term() was already called, return ETERM. */
    if (nn_slow (self->flags & (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING)))
        return -ETERM;

    /*  Protocol-specific socket options. */
    if (level > NN_SOL_SOCKET)
        return self->sockbase->vfptr->setopt (self->sockbase, level, option,
            optval, optvallen);

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        optset = nn_sock_optset (self, level);
        if (!optset)
            return -ENOPROTOOPT;
        return optset->vfptr->setopt (optset, option, optval, optvallen);
    }

    /*  At this point we assume that all options are of type int. */
    if (optvallen != sizeof (int))
        return -EINVAL;
    val = *(int*) optval;

    /*  Generic socket-level options. */
    if (level == NN_SOL_SOCKET) {
        switch (option) {
        case NN_LINGER:
            dst = &self->linger;
            break;
        case NN_SNDBUF:
            if (nn_slow (val <= 0))
                return -EINVAL;
            dst = &self->sndbuf;
            break;
        case NN_RCVBUF:
            if (nn_slow (val <= 0))
                return -EINVAL;
            dst = &self->rcvbuf;
            break;
        case NN_SNDTIMEO:
            dst = &self->sndtimeo;
            break;
        case NN_RCVTIMEO:
            dst = &self->rcvtimeo;
            break;
        case NN_RECONNECT_IVL:
            if (nn_slow (val < 0))
                return -EINVAL;
            dst = &self->reconnect_ivl;
            break;
        case NN_RECONNECT_IVL_MAX:
            if (nn_slow (val < 0))
                return -EINVAL;
            dst = &self->reconnect_ivl_max;
            break;
        case NN_SNDPRIO:
            if (nn_slow (val < 1 || val > 16))
                return -EINVAL;
            dst = &self->sndprio;
            break;
        default:
            return -ENOPROTOOPT;
        }
        *dst = val;

        return 0;
    }

    nn_assert (0);
}

int nn_sock_getopt (struct nn_sock *self, int level, int option,
    void *optval, size_t *optvallen, int internal)
{
    int rc;

    if (!internal)
        nn_ctx_enter (&self->ctx);
    rc = nn_sock_getopt_inner (self, level, option, optval, optvallen);
    if (!internal)
        nn_ctx_leave (&self->ctx);

    return rc;
}

static int nn_sock_getopt_inner (struct nn_sock *self, int level,
    int option, void *optval, size_t *optvallen)
{
    int rc;
    struct nn_optset *optset;
    int intval;
    nn_fd fd;

    /*  If nn_term() was already called, return ETERM. */
    if (nn_slow (self->flags & (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING)))
        return -ETERM;

    /*  Generic socket-level options. */
    if (level == NN_SOL_SOCKET) {
        switch (option) {
        case NN_DOMAIN:
            intval = self->socktype->domain;
            break;
        case NN_PROTOCOL:
            intval = self->socktype->protocol;
            break;
        case NN_LINGER:
            intval = self->linger;
            break;
        case NN_SNDBUF:
            intval = self->sndbuf;
            break;
        case NN_RCVBUF:
            intval = self->rcvbuf;
            break;
        case NN_SNDTIMEO:
            intval = self->sndtimeo;
            break;
        case NN_RCVTIMEO:
            intval = self->rcvtimeo;
            break;
        case NN_RECONNECT_IVL:
            intval = self->reconnect_ivl;
            break;
        case NN_RECONNECT_IVL_MAX:
            intval = self->reconnect_ivl_max;
            break;
        case NN_SNDPRIO:
            intval = self->sndprio;
            break;
        case NN_SNDFD:
            if (self->socktype->flags & NN_SOCKTYPE_FLAG_NOSEND)
                return -ENOPROTOOPT;
            fd = nn_efd_getfd (&self->sndfd);
            memcpy (optval, &fd,
                *optvallen < sizeof (nn_fd) ? *optvallen : sizeof (nn_fd));
            *optvallen = sizeof (nn_fd);
            return 0;
        case NN_RCVFD:
            if (self->socktype->flags & NN_SOCKTYPE_FLAG_NORECV)
                return -ENOPROTOOPT;
            fd = nn_efd_getfd (&self->rcvfd);
            memcpy (optval, &fd,
                *optvallen < sizeof (nn_fd) ? *optvallen : sizeof (nn_fd));
            *optvallen = sizeof (nn_fd);
            return 0;
        default:
            return -ENOPROTOOPT;
        }

        memcpy (optval, &intval,
            *optvallen < sizeof (int) ? *optvallen : sizeof (int));
        *optvallen = sizeof (int);

        return 0;
    }

    /*  Protocol-specific socket options. */
    if (level > NN_SOL_SOCKET)
        return rc = self->sockbase->vfptr->getopt (self->sockbase,
            level, option, optval, optvallen);

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        optset = nn_sock_optset (self, level);
        if (!optset)
            return -ENOPROTOOPT;
        return optset->vfptr->getopt (optset, option, optval, optvallen);
    }

    nn_assert (0);
}

int nn_sock_add_ep (struct nn_sock *self, const char *addr,
    int (*factory) (const char *addr, void *hint, struct nn_epbase **ep))
{
    int rc;
    struct nn_epbase *ep;
    int eid;
    
    nn_ctx_leave (&self->ctx);

    /*  Create the transport-specific endpoint. */
    rc = factory (addr, (void*) self, &ep);
    if (nn_slow (rc < 0)) {
        nn_ctx_leave (&self->ctx);
        return rc;
    }

    /*  Provide it with an unique endpoint ID. */
    eid = ep->eid = self->eid;
    ++self->eid;

    /*  Add it to the list of active endpoints. */
    nn_list_insert (&self->eps, &ep->item, nn_list_end (&self->eps));

    nn_ctx_leave (&self->ctx);

    return eid;
}

int nn_sock_rm_ep (struct nn_sock *self, int eid)
{
    int rc;
    struct nn_list_item *it;
    struct nn_epbase *ep;

    nn_ctx_leave (&self->ctx);

    /*  Find the specified enpoint. */
    ep = NULL;
    for (it = nn_list_begin (&self->eps);
          it != nn_list_end (&self->eps);
          it = nn_list_next (&self->eps, it)) {
        ep = nn_cont (it, struct nn_epbase, item);
        if (ep->eid == eid)
            break;
        ep = NULL;
    }

    /*  The endpoint doesn't exist. */
    if (!ep) {
        nn_ctx_leave (&self->ctx);
        return -EINVAL;
    }
    
    /*  Ask the endpoint to shutdown. Actual terminatation may be delayed
        by the transport. */
    rc = nn_ep_close ((void*) ep);
    errnum_assert (rc == 0 || rc == -EINPROGRESS, -rc);

    nn_ctx_leave (&self->ctx);

    return 0;
}

void nn_sock_ep_closed (struct nn_sock *self, struct nn_epbase *ep)
{
    /*  Remove the endpoint from the list of active endpoints. */
    nn_list_erase (&self->eps, &ep->item);

    /*  nn_close() may be waiting for termination of this endpoint.
        Send it a signal. */
    if (self->flags & NN_SOCK_FLAG_CLOSING && nn_list_empty (&self->eps))
        nn_sem_post (&self->termsem);
}

int nn_sock_send (struct nn_sock *self, struct nn_msg *msg, int flags)
{
    int rc;
    uint64_t deadline;
    uint64_t now;
    int timeout;

    /*  Some sockets types cannot be used for sending messages. */
    if (nn_slow (self->socktype->flags & NN_SOCKTYPE_FLAG_NOSEND))
        return -ENOTSUP;

    nn_ctx_enter (&self->ctx);

    /*  Compute the deadline for SNDTIMEO timer. */
    if (self->sndtimeo < 0)
        timeout = -1;
    else {
        deadline = nn_clock_now (&self->clock) + self->sndtimeo;
        timeout = self->sndtimeo;
    }

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (self->flags &
              (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
            nn_ctx_leave (&self->ctx);
            return -ETERM;
        }

        /*  Try to send the message in a non-blocking way. */
        rc = self->sockbase->vfptr->send (self->sockbase, msg);
        if (nn_fast (rc == 0)) {
            nn_ctx_leave (&self->ctx);
            return 0;
        }
        nn_assert (rc < 0);

        /*  Any unexpected error is forwarded to the caller. */
        if (nn_slow (rc != -EAGAIN)) {
            nn_ctx_leave (&self->ctx);
            return rc;
        }

        /*  If the message cannot be sent at the moment and the send call
            is non-blocking, return immediately. */
        if (nn_fast (flags & NN_DONTWAIT)) {
            nn_ctx_leave (&self->ctx);
            return -EAGAIN;
        }

        /*  With blocking send, wait while there are new pipes available
            for sending. */
        nn_ctx_leave (&self->ctx);
        rc = nn_efd_wait (&self->sndfd, timeout);
        if (nn_slow (rc == -ETIMEDOUT))
            return -EAGAIN;
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, rc);
        nn_ctx_enter (&self->ctx);

        /*  If needed, re-compute the timeout to reflect the time that have
            already elapsed. */
        if (self->sndtimeo >= 0) {
            now = nn_clock_now (&self->clock);
            timeout = (int) (now > deadline ? 0 : deadline - now);
        }
    }   
}

int nn_sock_recv (struct nn_sock *self, struct nn_msg *msg, int flags)
{
    int rc;
    struct nn_sockbase *sockbase;
    uint64_t deadline;
    uint64_t now;
    int timeout;

    sockbase = (struct nn_sockbase*) self;

    /*  Some sockets types cannot be used for receiving messages. */
    if (nn_slow (self->socktype->flags & NN_SOCKTYPE_FLAG_NORECV))
        return -ENOTSUP;

    nn_ctx_enter (&self->ctx);

    /*  Compute the deadline for RCVTIMEO timer. */
    if (self->rcvtimeo < 0)
        timeout = -1;
    else {
        deadline = nn_clock_now (&self->clock) + self->rcvtimeo;
        timeout = self->rcvtimeo;
    }

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (self->flags &
              (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
            nn_ctx_leave (&self->ctx);
            return -ETERM;
        }

        /*  Try to receive the message in a non-blocking way. */
        rc = self->sockbase->vfptr->recv (self->sockbase, msg);
        if (nn_fast (rc == 0)) {
            nn_ctx_leave (&self->ctx);
            return 0;
        }
        nn_assert (rc < 0);

        /*  Any unexpected error is forwarded to the caller. */
        if (nn_slow (rc != -EAGAIN)) {
            nn_ctx_leave (&self->ctx);
            return rc;
        }

        /*  If the message cannot be received at the moment and the recv call
            is non-blocking, return immediately. */
        if (nn_fast (flags & NN_DONTWAIT)) {
            nn_ctx_leave (&self->ctx);
            return -EAGAIN;
        }

        /*  With blocking recv, wait while there are new pipes available
            for receiving. */
        nn_ctx_leave (&self->ctx);
        rc = nn_efd_wait (&self->rcvfd, timeout);
        if (nn_slow (rc == -ETIMEDOUT))
            return -EAGAIN;
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, rc);
        nn_ctx_enter (&self->ctx);

        /*  If needed, re-compute the timeout to reflect the time that have
            already elapsed. */
        if (self->rcvtimeo >= 0) {
            now = nn_clock_now (&self->clock);
            timeout = (int) (now > deadline ? 0 : deadline - now);
        }
    }  
}

int nn_sock_add (struct nn_sock *self, struct nn_pipe *pipe)
{
    return self->sockbase->vfptr->add (self->sockbase, pipe);
}

void nn_sock_rm (struct nn_sock *self, struct nn_pipe *pipe)
{
    self->sockbase->vfptr->rm (self->sockbase, pipe);
}

void nn_sock_in (struct nn_sock *self, struct nn_pipe *pipe)
{
    self->sockbase->vfptr->in (self->sockbase, pipe);
}

void nn_sock_out (struct nn_sock *self, struct nn_pipe *pipe)
{
    self->sockbase->vfptr->out (self->sockbase, pipe);
}

#if 0
void nn_sockbase_adjust_events (struct nn_sockbase *self)
{
    int events;

    /*  If nn_close() was already called there's no point in adjusting the
        snd/rcv file descriptors. */
    if (self->flags & NN_SOCK_FLAG_CLOSING)
        return;

    /*  Check whether socket is readable and/or writeable at the moment. */
    events = self->vfptr->events (self);
    errnum_assert (events >= 0, -events);

    /*  Signal/unsignal IN as needed. */
    if (!(self->vfptr->flags & NN_SOCKBASE_FLAG_NORECV)) {
        if (events & NN_SOCKBASE_EVENT_IN) {
            nn_assert (!(self->vfptr->flags & NN_SOCKBASE_FLAG_NORECV));
            if (!(self->flags & NN_SOCK_FLAG_IN)) {
                self->flags |= NN_SOCK_FLAG_IN;
                nn_efd_signal (&self->rcvfd);
            }
        }
        else {
            if (self->flags & NN_SOCK_FLAG_IN) {
                self->flags &= ~NN_SOCK_FLAG_IN;
                nn_efd_unsignal (&self->rcvfd);
            }
        }
    }

    /*  Signal/unsignal OUT as needed. */
    if (!(self->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND)) {
        if (events & NN_SOCKBASE_EVENT_OUT) {
            nn_assert (!(self->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND));
            if (!(self->flags & NN_SOCK_FLAG_OUT)) {
                self->flags |= NN_SOCK_FLAG_OUT;
                nn_efd_signal (&self->sndfd);
            }
        }
        else {
            if (self->flags & NN_SOCK_FLAG_OUT) {
                self->flags &= ~NN_SOCK_FLAG_OUT;
                nn_efd_unsignal (&self->sndfd);
            }
        }
    }
}
#endif

struct nn_optset *nn_sock_optset (struct nn_sock *self, int id)
{
    int index;
    struct nn_transport *tp;

    /*  Transport IDs are negative and start from -1. */
    index = (-id) - 1;

    /*  Check for invalid indices. */
    if (nn_slow (index < 0 || index >= NN_MAX_TRANSPORT))
        return NULL;

    /*  If the option set already exists return it. */
    if (nn_fast (self->optsets [index] != NULL))
        return self->optsets [index];

    /*  If the option set doesn't exist yet, create it. */
    tp = nn_global_transport (id);
    if (nn_slow (!tp))
        return NULL;
    if (nn_slow (!tp->optset))
        return NULL;
    self->optsets [index] = tp->optset ();

    return self->optsets [index];
}

