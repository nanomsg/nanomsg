/*
    Copyright (c) 2012 250bpm s.r.o.

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

#include "../pattern.h"
#include "../transport.h"

#include "sock.h"

#include "../utils/err.h"
#include "../utils/cont.h"

#define SP_SOCK_EVENT_IN 1
#define SP_SOCK_EVENT_OUT 2

/*  sp_cp callbacks. */
static void sp_sock_event (struct sp_cp *self, int event,
    struct sp_event_hndl *hndl);
static const struct sp_cp_vfptr sp_sock_cp_vfptr = {
    sp_sock_event
};

void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    self->vfptr = vfptr;
    sp_cp_init (&self->cp, &sp_sock_cp_vfptr);
    sp_cond_init (&self->cond);
    self->fd = fd;
}

void sp_sock_term (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    /*  Terminate the derived class. */
    sockbase->vfptr->term (sockbase);

    /*  Terminate the sp_sockbase itself. */
    sp_cond_term (&sockbase->cond);
    sp_cp_term (&sockbase->cp);
}

struct sp_cp *sp_sock_getcp (struct sp_sock *self)
{
    return &((struct sp_sockbase*) self)->cp;
}

int sp_sock_setopt (struct sp_sock *self, int level, int option,
    const void *optval, size_t optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cp_lock (&sockbase->cp);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->setopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_cp_unlock (&sockbase->cp);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_cp_unlock (&sockbase->cp);
    return -ENOPROTOOPT;
}

int sp_sock_getopt (struct sp_sock *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cp_lock (&sockbase->cp);

    /*  TODO: Handle socket-leven options here. */

    /*  Unknown options may be pattern-specific. */
    if (level == SP_SOL_SOCKET) {
        rc = sockbase->vfptr->getopt (sockbase, option, optval, optvallen);
        if (rc != -ENOPROTOOPT) {
            sp_cp_unlock (&sockbase->cp);
            return rc;
        }
    }

    /*   TODO: Check transport-specific options here. */

    /*  Socket option not found. */
    sp_cp_unlock (&sockbase->cp);
    return -ENOPROTOOPT;
}

int sp_sock_send (struct sp_sock *self, const void *buf, size_t len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cp_lock (&sockbase->cp);

    while (1) {

        /*  Try to send the message in a non-blocking way. */
        rc = sockbase->vfptr->send (sockbase, buf, len);
        if (sp_fast (rc == 0)) {
            sp_cp_unlock (&sockbase->cp);
            return 0;
        }

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN)) {
            sp_cp_unlock (&sockbase->cp);
            return rc;
        }

        /*  If the message cannot be sent at the moment and the send call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT)) {
            sp_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }

        /*  With blocking send, wait while there are new pipes available
            for sending. */
        rc = sp_cond_wait (&sockbase->cond, &sockbase->cp, -1);
        errnum_assert (rc == 0, rc);
    }   
}

int sp_sock_recv (struct sp_sock *self, void *buf, size_t *len, int flags)
{
    int rc;
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;

    sp_cp_lock (&sockbase->cp);

    while (1) {

        /*  Try to receive the message in a non-blocking way. */
        rc = sockbase->vfptr->recv (sockbase, buf, len);
        if (sp_fast (rc == 0)) {
            sp_cp_unlock (&sockbase->cp);
            return 0;
        }

        /*  Any unexpected error is forwarded to the caller. */
        if (sp_slow (rc != -EAGAIN)) {
            sp_cp_unlock (&sockbase->cp);
            return rc;
        }

        /*  If the message cannot be received at the moment and the recv call
            is non-blocking, return immediately. */
        if (sp_fast (flags & SP_DONTWAIT)) {
            sp_cp_unlock (&sockbase->cp);
            return -EAGAIN;
        }

        /*  With blocking recv, wait while there are new pipes available
            for receiving. */
        rc = sp_cond_wait (&sockbase->cond, &sockbase->cp, -1);
        errnum_assert (rc == 0, rc);
    }  
}

int sp_sock_fd (struct sp_sock *self)
{
    struct sp_sockbase *sockbase;

    sockbase = (struct sp_sockbase*) self;
    return sockbase->fd;
}

int sp_sock_add (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct sp_sockbase*) self;
    return sockbase->vfptr->add (sockbase, pipe);
}

void sp_sock_rm (struct sp_sock *self, struct sp_pipe *pipe)
{
    struct sp_sockbase *sockbase;

    /*  Forward the call to the specific socket type. */
    sockbase = (struct sp_sockbase*) self;
    sockbase->vfptr->rm (sockbase, pipe);
}

void sp_sock_in (struct sp_sock *self, struct sp_pipe *pipe)
{
    sp_cp_post (&((struct sp_sockbase*) self)->cp, SP_SOCK_EVENT_IN,
        &((struct sp_pipebase*) pipe)->inevent);
}

void sp_sock_out (struct sp_sock *self, struct sp_pipe *pipe)
{
    sp_cp_post (&((struct sp_sockbase*) self)->cp, SP_SOCK_EVENT_OUT,
        &((struct sp_pipebase*) pipe)->outevent);
}

static void sp_sock_event (struct sp_cp *self, int event,
    struct sp_event_hndl *hndl)
{
    int rc;
    struct sp_sockbase *sockbase;
    struct sp_pipebase *pipebase;

    sockbase = sp_cont (self, struct sp_sockbase, cp);

    switch (event) {
    case SP_SOCK_EVENT_IN:
        pipebase = sp_cont (hndl, struct sp_pipebase, inevent);
        rc = sockbase->vfptr->in (sockbase, (struct sp_pipe*) pipebase);
        errnum_assert (rc >= 0, -rc);
        if (rc == 1)
            sp_cond_post (&sockbase->cond);
        return;
    case SP_SOCK_EVENT_OUT:
        pipebase = sp_cont (hndl, struct sp_pipebase, outevent);
        rc = sockbase->vfptr->out (sockbase, (struct sp_pipe*) pipebase);
        errnum_assert (rc >= 0, -rc);
        if (rc == 1)
            sp_cond_post (&sockbase->cond);
        return;
    default:
        sp_assert (0);
    }
}

void sp_sockbase_add_timer (struct sp_sockbase *self, int timeout,
    const struct sp_cp_timer_vfptr *vfptr, struct sp_cp_timer_hndl *hndl)
{
    sp_cp_add_timer (&self->cp, timeout, vfptr, hndl);
}

void sp_sockbase_rm_timer (struct sp_sockbase *self,
    struct sp_cp_timer_hndl *hndl)
{
    sp_cp_rm_timer (&self->cp, hndl);
}

