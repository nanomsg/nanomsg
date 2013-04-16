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

#include "../transport.h"
#include "../protocol.h"

#include "sock.h"

#include "../utils/err.h"
#include "../utils/fast.h"

/*  Internal pipe states. */
#define NN_PIPEBASE_INSTATE_DEACTIVATED 0
#define NN_PIPEBASE_INSTATE_IDLE 1
#define NN_PIPEBASE_INSTATE_RECEIVING 2
#define NN_PIPEBASE_INSTATE_RECEIVED 3
#define NN_PIPEBASE_INSTATE_ASYNC 4
#define NN_PIPEBASE_OUTSTATE_DEACTIVATED 0
#define NN_PIPEBASE_OUTSTATE_IDLE 1
#define NN_PIPEBASE_OUTSTATE_SENDING 2
#define NN_PIPEBASE_OUTSTATE_SENT 3
#define NN_PIPEBASE_OUTSTATE_ASYNC 4

int nn_pipebase_init (struct nn_pipebase *self,
    const struct nn_pipebase_vfptr *vfptr, struct nn_epbase *epbase)
{
    nn_assert (epbase->sock);
    self->vfptr = vfptr;
    self->instate = NN_PIPEBASE_INSTATE_DEACTIVATED;
    self->outstate = NN_PIPEBASE_OUTSTATE_DEACTIVATED;
    self->sock = epbase->sock;
    return nn_sock_add (self->sock, (struct nn_pipe*) self);
}

void nn_pipebase_term (struct nn_pipebase *self)
{
    if (self->sock)
        nn_sock_rm (self->sock, (struct nn_pipe*) self);
}

void nn_pipebase_activate (struct nn_pipebase *self)
{
    self->instate = NN_PIPEBASE_INSTATE_ASYNC;
    self->outstate = NN_PIPEBASE_OUTSTATE_IDLE;

    /*  Provide the outgoing pipe to the SP socket. */
    if (self->sock)
        nn_sock_out (self->sock, (struct nn_pipe*) self);
}

void nn_pipebase_received (struct nn_pipebase *self)
{
    if (nn_fast (self->instate == NN_PIPEBASE_INSTATE_RECEIVING)) {
        self->instate = NN_PIPEBASE_INSTATE_RECEIVED;
        return;
    }
    nn_assert (self->instate == NN_PIPEBASE_INSTATE_ASYNC);
    self->instate = NN_PIPEBASE_INSTATE_IDLE;
    if (self->sock)
        nn_sock_in (self->sock, (struct nn_pipe*) self);
}

void nn_pipebase_sent (struct nn_pipebase *self)
{
    if (nn_fast (self->outstate == NN_PIPEBASE_OUTSTATE_SENDING)) {
        self->outstate = NN_PIPEBASE_OUTSTATE_SENT;
        return;
    }
    nn_assert (self->outstate == NN_PIPEBASE_OUTSTATE_ASYNC);
    self->outstate = NN_PIPEBASE_OUTSTATE_IDLE;
    if (self->sock)
        nn_sock_out (self->sock, (struct nn_pipe*) self);
}

struct nn_cp *nn_pipebase_getcp (struct nn_pipebase *self)
{
    return nn_sock_getcp (self->sock);
}

int nn_pipebase_ispeer (struct nn_pipebase *self, int socktype)
{
    return nn_sock_ispeer (self->sock, socktype);
}

void nn_pipe_setdata (struct nn_pipe *self, void *data)
{
    ((struct nn_pipebase*) self)->data = data;
}

void *nn_pipe_getdata (struct nn_pipe *self)
{
    return ((struct nn_pipebase*) self)->data;
}

int nn_pipe_send (struct nn_pipe *self, struct nn_msg *msg)
{
    int rc;
    struct nn_pipebase *pipebase;

    pipebase = (struct nn_pipebase*) self;
    nn_assert (pipebase->outstate == NN_PIPEBASE_OUTSTATE_IDLE);
    pipebase->outstate = NN_PIPEBASE_OUTSTATE_SENDING;
    rc = pipebase->vfptr->send (pipebase, msg);
    errnum_assert (rc >= 0, -rc);
    if (nn_fast (pipebase->outstate == NN_PIPEBASE_OUTSTATE_SENT)) {
        pipebase->outstate = NN_PIPEBASE_OUTSTATE_IDLE;
        return rc;
    }
    nn_assert (pipebase->outstate == NN_PIPEBASE_OUTSTATE_SENDING);
    pipebase->outstate = NN_PIPEBASE_OUTSTATE_ASYNC;
    return rc | NN_PIPEBASE_RELEASE;
}

int nn_pipe_recv (struct nn_pipe *self, struct nn_msg *msg)
{
    int rc;
    struct nn_pipebase *pipebase;

    pipebase = (struct nn_pipebase*) self;
    nn_assert (pipebase->instate == NN_PIPEBASE_INSTATE_IDLE);
    pipebase->instate = NN_PIPEBASE_INSTATE_RECEIVING;
    rc = pipebase->vfptr->recv (pipebase, msg);
    errnum_assert (rc >= 0, -rc);

    if (nn_fast (pipebase->instate == NN_PIPEBASE_INSTATE_RECEIVED)) {
        pipebase->instate = NN_PIPEBASE_INSTATE_IDLE;
        return rc;
    }
    nn_assert (pipebase->instate == NN_PIPEBASE_INSTATE_RECEIVING);
    pipebase->instate = NN_PIPEBASE_INSTATE_ASYNC;
    return rc | NN_PIPEBASE_RELEASE;
}

