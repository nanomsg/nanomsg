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
#include "ep.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/fast.h"
#include "../utils/latmon.h"
#include "../utils/msg.h"

#define NN_SOCK_EVENT_IN 1
#define NN_SOCK_EVENT_OUT 2

void nn_sockbase_init (struct nn_sockbase *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    self->vfptr = vfptr;
    self->flags = 0;
    nn_cp_init (&self->cp);
    nn_cond_init (&self->cond);
    nn_clock_init (&self->clock);
    self->fd = fd;
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
    self->rcvprio = 8;
}

void nn_sock_zombify (struct nn_sock *self)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;
    nn_cp_lock (&sockbase->cp);
    sockbase->flags |= NN_SOCK_FLAG_ZOMBIE;
    nn_cond_post (&sockbase->cond);
    nn_cp_unlock (&sockbase->cp);
}

void nn_sock_term (struct nn_sock *self)
{
    int rc;
    struct nn_sockbase *sockbase;
    struct nn_list_item *it;
    struct nn_epbase *ep;

    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  Ask all the associated endpoints to terminate. Call to nn_ep_close can
        actually deallocate the endpoint, so take care to get pointer to the
        next endpoint before the call. */
    it = nn_list_begin (&sockbase->eps);
    while (it != nn_list_end (&sockbase->eps)) {
        ep = nn_cont (it, struct nn_epbase, item);
        it = nn_list_next (&sockbase->eps, it);
        nn_ep_close ((void*) ep);       
    }

    while (1) {

        /*  If there are no active endpoints we can deallocate the socket
            straight away. */
        if (nn_list_empty (&sockbase->eps)) {

            /*  Terminate the nn_sockbase itself. */
            nn_cp_unlock (&sockbase->cp);
            nn_list_term (&sockbase->eps);
            nn_clock_term (&sockbase->clock);
            nn_cond_term (&sockbase->cond);
            nn_cp_term (&sockbase->cp);

            /*  Deallocate the derived class. */
            sockbase->vfptr->destroy (sockbase);

            return;
        }

        /*  Wait till all the endpoints are closed. */
        nn_cond_set_timeout (&sockbase->cond, -1);
        rc = nn_cond_wait (&sockbase->cond, &sockbase->cp.sync);
        errnum_assert (rc == 0, rc);
    }
}

void nn_sockbase_unblock_recv (struct nn_sockbase *self)
{
    nn_cond_post (&self->cond);
}

void nn_sockbase_unblock_send (struct nn_sockbase *self)
{
    nn_cond_post (&self->cond);
}

struct nn_cp *nn_sockbase_getcp (struct nn_sockbase *self)
{
    return &self->cp;
}

struct nn_cp *nn_sock_getcp (struct nn_sock *self)
{
    return &((struct nn_sockbase*) self)->cp;
}

int nn_sock_setopt (struct nn_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;
    struct nn_sockbase *sockbase;
    int val;
    int *dst;

    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  If nn_term() was already called, return ETERM. */
    if (nn_slow (sockbase->flags & NN_SOCK_FLAG_ZOMBIE)) {
        nn_cp_unlock (&sockbase->cp);
        return -ETERM;
    }

    /*  Protocol-specific socket options. */
    if (level > NN_SOL_SOCKET) {
        rc = sockbase->vfptr->setopt (sockbase, level, option,
            optval, optvallen);
        nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        nn_cp_unlock (&sockbase->cp);
        return -ENOPROTOOPT;
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
    int *src;

    sockbase = (struct nn_sockbase*) self;

    if (!internal)
        nn_cp_lock (&sockbase->cp);

    /*  If nn_term() was already called, return ETERM. */
    if (!internal && nn_slow (sockbase->flags & NN_SOCK_FLAG_ZOMBIE)) {
        nn_cp_unlock (&sockbase->cp);
        return -ETERM;
    }

    /*  Generic socket-level options. */
    if (level == NN_SOL_SOCKET) {
        switch (option) {
        case NN_LINGER:
            src = &sockbase->linger;
            break;
        case NN_SNDBUF:
            src = &sockbase->sndbuf;
            break;
        case NN_RCVBUF:
            src = &sockbase->rcvbuf;
            break;
        case NN_SNDTIMEO:
            src = &sockbase->sndtimeo;
            break;
        case NN_RCVTIMEO:
            src = &sockbase->rcvtimeo;
            break;
        case NN_RECONNECT_IVL:
            src = &sockbase->reconnect_ivl;
            break;
        case NN_RECONNECT_IVL_MAX:
            src = &sockbase->reconnect_ivl_max;
            break;
        case NN_SNDPRIO:
            src = &sockbase->sndprio;
            break;
        case NN_SNDFD:
        case NN_RCVFD:
            nn_assert (0);
        default:
            if (!internal)
                nn_cp_unlock (&sockbase->cp);
            return -ENOPROTOOPT;
        }
        memcpy (optval, src,
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
        if (!internal)
            nn_cp_unlock (&sockbase->cp);
        return rc;
    }

    /*  Transport-specific options. */
    if (level < NN_SOL_SOCKET) {
        if (!internal)
            nn_cp_unlock (&sockbase->cp);
        return -ENOPROTOOPT;
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
    nn_ep_close ((void*) ep);

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
    if (nn_list_empty (&sockbase->eps))
        nn_cond_post (&sockbase->cond);
}

int nn_sock_send (struct nn_sock *self, struct nn_msg *msg, int flags)
{
    int rc;
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  Set the SNDTIMEO timer. */
    nn_cond_set_timeout (&sockbase->cond, sockbase->sndtimeo);

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (sockbase->flags & NN_SOCK_FLAG_ZOMBIE)) {
            nn_cp_unlock (&sockbase->cp);
            return -ETERM;
        }

        /*  Try to send the message in a non-blocking way. */
        rc = sockbase->vfptr->send (sockbase, msg);
        if (nn_fast (rc == 0)) {
            nn_cp_unlock (&sockbase->cp);
            return 0;
        }

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
        rc = nn_cond_wait (&sockbase->cond, &sockbase->cp.sync);
        if (nn_slow (rc == -ETIMEDOUT)) {
            nn_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }
        errnum_assert (rc == 0, rc);
    }   
}

int nn_sock_recv (struct nn_sock *self, struct nn_msg *msg, int flags)
{
    int rc;
    struct nn_sockbase *sockbase;


    sockbase = (struct nn_sockbase*) self;

    nn_cp_lock (&sockbase->cp);

    /*  Set the RCVTIMEO timer. */
    nn_cond_set_timeout (&sockbase->cond, sockbase->rcvtimeo);

    while (1) {

        /*  If nn_term() was already called, return ETERM. */
        if (nn_slow (sockbase->flags & NN_SOCK_FLAG_ZOMBIE)) {
            nn_cp_unlock (&sockbase->cp);
            return -ETERM;
        }

        /*  Try to receive the message in a non-blocking way. */
        rc = sockbase->vfptr->recv (sockbase, msg);
        if (nn_fast (rc == 0)) {
            nn_cp_unlock (&sockbase->cp);
            return 0;
        }

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
        rc = nn_cond_wait (&sockbase->cond, &sockbase->cp.sync);
        if (nn_slow (rc == -ETIMEDOUT)) {
            nn_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }
        errnum_assert (rc == 0, rc);
#if defined NN_LATENCY_MONITOR
        nn_latmon_measure (NN_LATMON_COND_EXIT);
#endif
    }  
}

int nn_sock_sethdr (struct nn_sock *self, struct nn_msg *msg,
    const void *hdr, size_t hdrlen)
{
    return ((struct nn_sockbase*) self)->vfptr->sethdr (msg, hdr, hdrlen);
}

int nn_sock_gethdr (struct nn_sock *self, struct nn_msg *msg,
    void *hdr, size_t *hdrlen)
{
    return ((struct nn_sockbase*) self)->vfptr->gethdr (msg, hdr, hdrlen);
}

int nn_sock_fd (struct nn_sock *self)
{
    struct nn_sockbase *sockbase;

    sockbase = (struct nn_sockbase*) self;
    return sockbase->fd;
}

int nn_sock_add (struct nn_sock *self, struct nn_pipe *pipe)
{
    struct nn_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct nn_sockbase*) self;
    return sockbase->vfptr->add (sockbase, pipe);
}

void nn_sock_rm (struct nn_sock *self, struct nn_pipe *pipe)
{
    struct nn_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct nn_sockbase*) self;
    sockbase->vfptr->rm (sockbase, pipe);
}

void nn_sock_in (struct nn_sock *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct nn_sockbase*) self;
    rc = sockbase->vfptr->in (sockbase, pipe);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1) {
#if defined NN_LATENCY_MONITOR
        nn_latmon_measure (NN_LATMON_COND_POST);
#endif
        nn_cond_post (&sockbase->cond);
    }
}

void nn_sock_out (struct nn_sock *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct nn_sockbase*) self;
    rc = sockbase->vfptr->out (sockbase, pipe);
    errnum_assert (rc >= 0, -rc);
    if (rc == 1)
        nn_cond_post (&sockbase->cond);
}

