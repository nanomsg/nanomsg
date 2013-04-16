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
void nn_sockbase_adjust_events (struct nn_sockbase *self);
struct nn_optset *nn_sockbase_optset (struct nn_sockbase *self, int id);

int nn_sockbase_init (struct nn_sockbase *self,
    const struct nn_sockbase_vfptr *vfptr)
{
    int rc;
    int i;

    /* Make sure that at least one message direction is supported. */
    nn_assert (!(vfptr->flags & NN_SOCKBASE_FLAG_NOSEND) ||
        !(vfptr->flags & NN_SOCKBASE_FLAG_NORECV));

    /*  Open the NN_SNDFD and NN_RCVFD efds. Do so, only if the socket type
        supports send/recv, as appropriate. */
    if (vfptr->flags & NN_SOCKBASE_FLAG_NOSEND)
        memset (&self->sndfd, 0xcd, sizeof (self->sndfd));
    else {
        rc = nn_efd_init (&self->sndfd);
        if (nn_slow (rc < 0))
            return rc;
    }
    if (vfptr->flags & NN_SOCKBASE_FLAG_NORECV)
        memset (&self->rcvfd, 0xcd, sizeof (self->rcvfd));
    else {
        rc = nn_efd_init (&self->rcvfd);
        if (nn_slow (rc < 0)) {
            if (!(vfptr->flags & NN_SOCKBASE_FLAG_NOSEND))
                nn_efd_term (&self->sndfd);
            return rc;
        }
    }
    memset (&self->termsem, 0xcd, sizeof (self->termsem));
    rc = nn_cp_init (&self->cp);
    if (nn_slow (rc < 0)) {
        if (!(vfptr->flags & NN_SOCKBASE_FLAG_NORECV))
            nn_efd_term (&self->rcvfd);
        if (!(vfptr->flags & NN_SOCKBASE_FLAG_NOSEND))
            nn_efd_term (&self->sndfd);
        return rc;
    }

    self->vfptr = vfptr;
    self->flags = 0;
    nn_clock_init (&self->clock);
    nn_list_init (&self->eps);
    self->eid = 1;

    /*  Default values for NN_SOL_SOCKET options. */
    self->domain = -1;
    self->protocol = -1;
    self->linger = 1000;
    self->sndbuf = 128 * 1024;
    self->rcvbuf = 128 * 1024;
    self->sndtimeo = -1;
    self->rcvtimeo = -1;
    self->reconnect_ivl = 100;
    self->reconnect_ivl_max = 0;
    self->sndprio = 8;
    self->rcvprio = 8;

    /*  The transport-specific options are not initialised immediately,
        rather, they are allocated later on when needed. */
    for (i = 0; i != NN_MAX_TRANSPORT; ++i)
        self->optsets [i] = NULL;

    return 0;
}

void nn_sock_zombify (struct nn_sock *self)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;
    nn_cp_lock (&sockbase->cp);
    sockbase->flags |= NN_SOCK_FLAG_ZOMBIE;

    /*  Reset IN and OUT events to unblock any polling function. */
    if (!(sockbase->flags & NN_SOCK_FLAG_CLOSING)) {
        if (!(sockbase->flags & NN_SOCK_FLAG_IN)) {
            sockbase->flags |= NN_SOCK_FLAG_IN;
            if (!(sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NORECV))
                nn_efd_signal (&sockbase->rcvfd);
        }
        if (!(sockbase->flags & NN_SOCK_FLAG_OUT)) {
            sockbase->flags |= NN_SOCK_FLAG_OUT;
            if (!(sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND))
                nn_efd_signal (&sockbase->sndfd);
        }
    }

    nn_cp_unlock (&sockbase->cp);
}

int nn_sock_destroy (struct nn_sock *self)
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_list_item *it;
    struct nn_epbase *ep;

    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  The call may have been interrupted by a singal and restarted afterwards.
        In such case don't do the following stuff again. */
    if (!(sockbase->flags & NN_SOCK_FLAG_CLOSING)) {

        /*  Mark the socket as being in process of shutting down. */
        sockbase->flags |= NN_SOCK_FLAG_CLOSING;

        /*  Close sndfd and rcvfd. This should make any current select/poll
            using SNDFD and/or RCVFD exit. */
        if (!(sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NORECV)) {
            nn_efd_term (&sockbase->rcvfd);
            memset (&sockbase->rcvfd, 0xcd, sizeof (sockbase->rcvfd));
        }
        if (!(sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND)) {
            nn_efd_term (&sockbase->sndfd);
            memset (&sockbase->sndfd, 0xcd, sizeof (sockbase->sndfd));
        }

        /*  Create a semaphore to wait on for all endpoint to terminate. */
        nn_sem_init (&sockbase->termsem);

        /*  Ask all the associated endpoints to terminate. Call to nn_ep_close
            can actually deallocate the endpoint, so take care to get pointer
            to the next endpoint before the call. */
        it = nn_list_begin (&sockbase->eps);
        while (it != nn_list_end (&sockbase->eps)) {
            ep = nn_cont (it, struct nn_epbase, item);
            it = nn_list_next (&sockbase->eps, it);
            rc = nn_ep_close ((void*) ep);
            errnum_assert (rc == 0 || rc == -EINPROGRESS, -rc);      
        }
    }

    /*  Shutdown process was already started but some endpoints are still
        alive. Here we are going to wait till they are all closed. */
    if (!nn_list_empty (&sockbase->eps)) {
        nn_cp_unlock (&sockbase->cp);
        rc = nn_sem_wait (&sockbase->termsem);
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, -rc);
        nn_cp_lock (&sockbase->cp);
        nn_assert (nn_list_empty (&sockbase->eps));
    }

    /*  Deallocation of the socket is done by asking the derived class
        to deallocate. Derived class, in turn will terminate the sockbase
        class. */
    nn_sem_term (&sockbase->termsem);
    sockbase->vfptr->destroy (sockbase);
    /*  At this point the socket is already deallocated, make sure
        that it is not used here any more. */

    return 0;
}

void nn_sockbase_term (struct nn_sockbase *self)
{
    int i;

    nn_assert (self->flags & NN_SOCK_FLAG_CLOSING);

    /*  The lock was done in nn_sock_destroy function. */
    nn_cp_unlock (&self->cp);

    /*  Destroy any optsets associated with the socket. */
    for (i = 0; i != NN_MAX_TRANSPORT; ++i)
        if (self->optsets [i])
            self->optsets [i]->vfptr->destroy (self->optsets [i]);

    nn_list_term (&self->eps);
    nn_clock_term (&self->clock);
    nn_cp_term (&self->cp);
}

void nn_sock_postinit (struct nn_sock *self, int domain, int protocol)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    nn_assert (sockbase->domain == -1 && sockbase->protocol == -1);
    sockbase->domain = domain;
    sockbase->protocol = protocol;
    nn_sockbase_adjust_events (sockbase);
}

void nn_sockbase_changed (struct nn_sockbase *self)
{
    nn_sockbase_adjust_events (self);
}

struct nn_cp *nn_sockbase_getcp (struct nn_sockbase *self)
{
    return &self->cp;
}

struct nn_cp *nn_sock_getcp (struct nn_sock *self)
{
    return &((struct nn_sockbase*) self)->cp;
}

struct nn_worker *nn_sock_choose_worker (struct nn_sock *self)
{
    return nn_global_choose_worker ();
}

int nn_sock_ispeer (struct nn_sock *self, int socktype)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    /*  If the peer implements a different SP protocol,
        it is not a valid peer. */
    if ((sockbase->protocol & 0xfff0) != (socktype  & 0xfff0))
        return 0;

    /*  As long as the peer speaks the same protocol, socket type itself
        decides which socket types are to be accepted. */
    return sockbase->vfptr->ispeer (socktype);
}

int nn_sock_setopt (struct nn_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_optset *optset;
    int val;
    int *dst;

    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  If nn_term() was already called, return ETERM. */
    if (nn_slow (sockbase->flags &
          (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
        nn_cp_unlock (&sockbase->cp);
        return -ETERM;
    }

    /*  Protocol-specific socket options. */
    if (level > NN_SOL_SOCKET) {
        rc = sockbase->vfptr->setopt (sockbase, level, option,
            optval, optvallen);
        nn_sockbase_adjust_events (sockbase);
        nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        optset = nn_sockbase_optset (sockbase, level);
        if (!optset) {
            nn_cp_unlock (&sockbase->cp);
            return -ENOPROTOOPT;
        }
        rc = optset->vfptr->setopt (optset, option, optval, optvallen);
        nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  At this point we assume that all options are of type int. */
    if (optvallen != sizeof (int)) {
        nn_cp_unlock (&sockbase->cp);
        return -EINVAL;
    }
    val = *(int*) optval;

    /*  Generic socket-level options. */
    if (level == NN_SOL_SOCKET) {
        switch (option) {
        case NN_LINGER:
            dst = &sockbase->linger;
            break;
        case NN_SNDBUF:
            if (nn_slow (val <= 0)) {
                nn_cp_unlock (&sockbase->cp);
                return -EINVAL;
            }
            dst = &sockbase->sndbuf;
            break;
        case NN_RCVBUF:
            if (nn_slow (val <= 0)) {
                nn_cp_unlock (&sockbase->cp);
                return -EINVAL;
            }
            dst = &sockbase->rcvbuf;
            break;
        case NN_SNDTIMEO:
            dst = &sockbase->sndtimeo;
            break;
        case NN_RCVTIMEO:
            dst = &sockbase->rcvtimeo;
            break;
        case NN_RECONNECT_IVL:
            if (nn_slow (val < 0)) {
                nn_cp_unlock (&sockbase->cp);
                return -EINVAL;
            }
            dst = &sockbase->reconnect_ivl;
            break;
        case NN_RECONNECT_IVL_MAX:
            if (nn_slow (val < 0)) {
                nn_cp_unlock (&sockbase->cp);
                return -EINVAL;
            }
            dst = &sockbase->reconnect_ivl_max;
            break;
        case NN_SNDPRIO:
            if (nn_slow (val < 1 || val > 16)) {
                nn_cp_unlock (&sockbase->cp);
                return -EINVAL;
            }
            dst = &sockbase->sndprio;
            break;
        default:
            nn_cp_unlock (&sockbase->cp);
            return -ENOPROTOOPT;
        }
        *dst = val;
        nn_cp_unlock (&sockbase->cp);
        return 0;
    }

    nn_assert (0);
}

int nn_sock_getopt (struct nn_sock *self, int level, int option,
    void *optval, size_t *optvallen, int internal)
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_optset *optset;
    int intval;
    nn_fd fd;

    sockbase = (struct nn_sockbase*) self;

    if (!internal)
        nn_cp_lock (&sockbase->cp);

    /*  If nn_term() was already called, return ETERM. */
    if (!internal && nn_slow (sockbase->flags &
          (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
        nn_cp_unlock (&sockbase->cp);
        return -ETERM;
    }

    /*  Generic socket-level options. */
    if (level == NN_SOL_SOCKET) {
        switch (option) {
        case NN_DOMAIN:
            intval = sockbase->domain;
            break;
        case NN_PROTOCOL:
            intval = sockbase->protocol;
            break;
        case NN_LINGER:
            intval = sockbase->linger;
            break;
        case NN_SNDBUF:
            intval = sockbase->sndbuf;
            break;
        case NN_RCVBUF:
            intval = sockbase->rcvbuf;
            break;
        case NN_SNDTIMEO:
            intval = sockbase->sndtimeo;
            break;
        case NN_RCVTIMEO:
            intval = sockbase->rcvtimeo;
            break;
        case NN_RECONNECT_IVL:
            intval = sockbase->reconnect_ivl;
            break;
        case NN_RECONNECT_IVL_MAX:
            intval = sockbase->reconnect_ivl_max;
            break;
        case NN_SNDPRIO:
            intval = sockbase->sndprio;
            break;
        case NN_SNDFD:
            if (sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND) {
                if (!internal)
                    nn_cp_unlock (&sockbase->cp);
                return -ENOPROTOOPT;
            }
            fd = nn_efd_getfd (&sockbase->sndfd);
            memcpy (optval, &fd,
                *optvallen < sizeof (nn_fd) ? *optvallen : sizeof (nn_fd));
            *optvallen = sizeof (nn_fd);
            if (!internal)
                nn_cp_unlock (&sockbase->cp);
            return 0;
        case NN_RCVFD:
            if (sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NORECV) {
                if (!internal)
                    nn_cp_unlock (&sockbase->cp);
                return -ENOPROTOOPT;
            }
            fd = nn_efd_getfd (&sockbase->rcvfd);
            memcpy (optval, &fd,
                *optvallen < sizeof (nn_fd) ? *optvallen : sizeof (nn_fd));
            *optvallen = sizeof (nn_fd);
            if (!internal)
                nn_cp_unlock (&sockbase->cp);
            return 0;
        default:
            if (!internal)
                nn_cp_unlock (&sockbase->cp);
            return -ENOPROTOOPT;
        }
        memcpy (optval, &intval,
            *optvallen < sizeof (int) ? *optvallen : sizeof (int));
        *optvallen = sizeof (int);
        if (!internal)
            nn_cp_unlock (&sockbase->cp);
        return 0;
    }

    /*  Protocol-specific socket options. */
    if (level > NN_SOL_SOCKET) {
        rc = sockbase->vfptr->getopt (sockbase, level, option,
            optval, optvallen);
        nn_sockbase_adjust_events (sockbase);
        if (!internal)
            nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        optset = nn_sockbase_optset (sockbase, level);
        if (!optset) {
            if (!internal)
                nn_cp_unlock (&sockbase->cp);
            return -ENOPROTOOPT;
        }
        rc = optset->vfptr->getopt (optset, option, optval, optvallen);
        if (!internal)
            nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    nn_assert (0);
}

int nn_sock_add_ep (struct nn_sock *self, const char *addr,
    int (*factory) (const char *addr, void *hint, struct nn_epbase **ep))
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_epbase *ep;
    int eid;
    
    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  Create the transport-specific endpoint. */
    rc = factory (addr, (void*) self, &ep);
    if (nn_slow (rc < 0)) {
        nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  Provide it with an unique endpoint ID. */
    eid = ep->eid = sockbase->eid;
    ++sockbase->eid;

    /*  Add it to the list of active endpoints. */
    nn_list_insert (&sockbase->eps, &ep->item, nn_list_end (&sockbase->eps));

    nn_cp_unlock (&sockbase->cp);

    return eid;
}

int nn_sock_rm_ep (struct nn_sock *self, int eid)
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_list_item *it;
    struct nn_epbase *ep;
    
    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  Find the specified enpoint. */
    ep = NULL;
    for (it = nn_list_begin (&sockbase->eps);
          it != nn_list_end (&sockbase->eps);
          it = nn_list_next (&sockbase->eps, it)) {
        ep = nn_cont (it, struct nn_epbase, item);
        if (ep->eid == eid)
            break;
        ep = NULL;
    }

    /*  The endpoint doesn't exist. */
    if (!ep) {
        nn_cp_unlock (&sockbase->cp);
        return -EINVAL;
    }
    
    /*  Ask the endpoint to shutdown. Actual terminatation may be delayed
        by the transport. */
    rc = nn_ep_close ((void*) ep);
    errnum_assert (rc == 0 || rc == -EINPROGRESS, -rc);

    nn_cp_unlock (&sockbase->cp);

    return 0;
}

void nn_sock_ep_closed (struct nn_sock *self, struct nn_epbase *ep)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    /*  Remove the endpoint from the list of active endpoints. */
    nn_list_erase (&sockbase->eps, &ep->item);

    /*  nn_close() may be waiting for termination of this endpoint.
        Send it a signal. */
    if (sockbase->flags & NN_SOCK_FLAG_CLOSING &&
          nn_list_empty (&sockbase->eps))
        nn_sem_post (&sockbase->termsem);
}

int nn_sock_send (struct nn_sock *self, struct nn_msg *msg, int flags)
{
    int rc;
    struct nn_sockbase *sockbase;
    uint64_t deadline;
    uint64_t now;
    int timeout;

    sockbase = (struct nn_sockbase*) self;

    /*  Some sockets types cannot be used for sending messages. */
    if (nn_slow (sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NOSEND))
        return -ENOTSUP;

    nn_cp_lock (&sockbase->cp);

    /*  Compute the deadline for SNDTIMEO timer. */
    if (sockbase->sndtimeo < 0)
        timeout = -1;
    else {
        deadline = nn_clock_now (&sockbase->clock) + sockbase->sndtimeo;
        timeout = sockbase->sndtimeo;
    }

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (sockbase->flags &
              (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
            nn_cp_unlock (&sockbase->cp);
            return -ETERM;
        }

        /*  Try to send the message in a non-blocking way. */
        rc = sockbase->vfptr->send (sockbase, msg);
        nn_sockbase_adjust_events (sockbase);
        if (nn_fast (rc == 0)) {
            nn_cp_unlock (&sockbase->cp);
            return 0;
        }
        nn_assert (rc < 0);

        /*  Any unexpected error is forwarded to the caller. */
        if (nn_slow (rc != -EAGAIN)) {
            nn_cp_unlock (&sockbase->cp);
            return rc;
        }

        /*  If the message cannot be sent at the moment and the send call
            is non-blocking, return immediately. */
        if (nn_fast (flags & NN_DONTWAIT)) {
            nn_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }

        /*  With blocking send, wait while there are new pipes available
            for sending. */
        nn_cp_unlock (&sockbase->cp);
        rc = nn_efd_wait (&sockbase->sndfd, timeout);
        if (nn_slow (rc == -ETIMEDOUT))
            return -EAGAIN;
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, rc);
        nn_cp_lock (&sockbase->cp);

        /*  If needed, re-compute the timeout to reflect the time that have
            already elapsed. */
        if (sockbase->sndtimeo >= 0) {
            now = nn_clock_now (&sockbase->clock);
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
    if (nn_slow (sockbase->vfptr->flags & NN_SOCKBASE_FLAG_NORECV))
        return -ENOTSUP;

    nn_cp_lock (&sockbase->cp);

    /*  Compute the deadline for RCVTIMEO timer. */
    if (sockbase->rcvtimeo < 0)
        timeout = -1;
    else {
        deadline = nn_clock_now (&sockbase->clock) + sockbase->rcvtimeo;
        timeout = sockbase->rcvtimeo;
    }

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (sockbase->flags &
              (NN_SOCK_FLAG_ZOMBIE | NN_SOCK_FLAG_CLOSING))) {
            nn_cp_unlock (&sockbase->cp);
            return -ETERM;
        }

        /*  Try to receive the message in a non-blocking way. */
        rc = sockbase->vfptr->recv (sockbase, msg);
        nn_sockbase_adjust_events (sockbase);
        if (nn_fast (rc == 0)) {
            nn_cp_unlock (&sockbase->cp);
            return 0;
        }
        nn_assert (rc < 0);

        /*  Any unexpected error is forwarded to the caller. */
        if (nn_slow (rc != -EAGAIN)) {
            nn_cp_unlock (&sockbase->cp);
            return rc;
        }

        /*  If the message cannot be received at the moment and the recv call
            is non-blocking, return immediately. */
        if (nn_fast (flags & NN_DONTWAIT)) {
            nn_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }

        /*  With blocking recv, wait while there are new pipes available
            for receiving. */
        nn_cp_unlock (&sockbase->cp);
        rc = nn_efd_wait (&sockbase->rcvfd, timeout);
        if (nn_slow (rc == -ETIMEDOUT))
            return -EAGAIN;
        if (nn_slow (rc == -EINTR))
            return -EINTR;
        errnum_assert (rc == 0, rc);
        nn_cp_lock (&sockbase->cp);

        /*  If needed, re-compute the timeout to reflect the time that have
            already elapsed. */
        if (sockbase->rcvtimeo >= 0) {
            now = nn_clock_now (&sockbase->clock);
            timeout = (int) (now > deadline ? 0 : deadline - now);
        }
    }  
}

int nn_sock_add (struct nn_sock *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    rc = sockbase->vfptr->add (sockbase, pipe);
    nn_sockbase_adjust_events (sockbase);
    return rc;
}

void nn_sock_rm (struct nn_sock *self, struct nn_pipe *pipe)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    sockbase->vfptr->rm (sockbase, pipe);
    nn_sockbase_adjust_events (sockbase);
}

void nn_sock_in (struct nn_sock *self, struct nn_pipe *pipe)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    sockbase->vfptr->in (sockbase, pipe);
    nn_sockbase_adjust_events (sockbase);
}

void nn_sock_out (struct nn_sock *self, struct nn_pipe *pipe)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    sockbase->vfptr->out (sockbase, pipe);
    nn_sockbase_adjust_events (sockbase);
}

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

struct nn_optset *nn_sockbase_optset (struct nn_sockbase *self, int id)
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

