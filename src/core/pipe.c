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

int sp_pipebase_init (struct sp_pipebase *self,
    const struct sp_pipebase_vfptr *vfptr, struct sp_epbase *epbase)
{
    sp_assert (epbase->sock);
    self->vfptr = vfptr;
    self->instate = SP_PIPEBASE_INSTATE_DEACTIVATED;
    self->outstate = SP_PIPEBASE_OUTSTATE_DEACTIVATED;
    self->epbase = epbase;
    return sp_sock_add (self->epbase->sock, (struct sp_pipe*) self);
}

void sp_pipebase_term (struct sp_pipebase *self)
{
    if (self->epbase->sock)
        sp_sock_rm (self->epbase->sock, (struct sp_pipe*) self);
}

void sp_pipebase_activate (struct sp_pipebase *self)
{
    self->instate = SP_PIPEBASE_INSTATE_ASYNC;
    self->outstate = SP_PIPEBASE_OUTSTATE_IDLE;

    /*  Provide the outgoing pipe to the SP socket. */
    if (self->epbase->sock)
        sp_sock_out (self->epbase->sock, (struct sp_pipe*) self);
}

void sp_pipebase_received (struct sp_pipebase *self)
{
    if (sp_fast (self->instate == SP_PIPEBASE_INSTATE_RECEIVING)) {
        self->instate = SP_PIPEBASE_INSTATE_RECEIVED;
        return;
    }
    sp_assert (self->instate == SP_PIPEBASE_INSTATE_ASYNC);
    self->instate = SP_PIPEBASE_INSTATE_IDLE;
    if (self->epbase->sock)
        sp_sock_in (self->epbase->sock, (struct sp_pipe*) self);
}

void sp_pipebase_sent (struct sp_pipebase *self)
{
    if (sp_fast (self->outstate == SP_PIPEBASE_OUTSTATE_SENDING)) {
        self->outstate = SP_PIPEBASE_OUTSTATE_SENT;
        return;
    }
    sp_assert (self->outstate == SP_PIPEBASE_OUTSTATE_ASYNC);
    self->outstate = SP_PIPEBASE_OUTSTATE_IDLE;
    if (self->epbase->sock)
        sp_sock_out (self->epbase->sock, (struct sp_pipe*) self);
}

struct sp_cp *sp_pipebase_getcp (struct sp_pipebase *self)
{
    return sp_epbase_getcp (self->epbase);
}

void sp_pipe_setdata (struct sp_pipe *self, void *data)
{
    ((struct sp_pipebase*) self)->data = data;
}

void *sp_pipe_getdata (struct sp_pipe *self)
{
    return ((struct sp_pipebase*) self)->data;
}

int sp_pipe_send (struct sp_pipe *self, const void *buf1, size_t len1,
    const void *buf2, size_t len2)
{
    struct sp_pipebase *pipebase;

    pipebase = (struct sp_pipebase*) self;
    sp_assert (pipebase->outstate == SP_PIPEBASE_OUTSTATE_IDLE);
    pipebase->outstate = SP_PIPEBASE_OUTSTATE_SENDING;
    pipebase->vfptr->send (pipebase, buf1, len1, buf2, len2);
    if (sp_fast (pipebase->outstate == SP_PIPEBASE_OUTSTATE_SENT)) {
        pipebase->outstate = SP_PIPEBASE_OUTSTATE_IDLE;
        return 0;
    }
    sp_assert (pipebase->outstate == SP_PIPEBASE_OUTSTATE_SENDING);
    pipebase->outstate = SP_PIPEBASE_OUTSTATE_ASYNC;
    return SP_PIPEBASE_RELEASE;
}

int sp_pipe_recv (struct sp_pipe *self, void *buf, size_t *len)
{
    struct sp_pipebase *pipebase;

    pipebase = (struct sp_pipebase*) self;
    sp_assert (pipebase->instate == SP_PIPEBASE_INSTATE_IDLE);
    pipebase->instate = SP_PIPEBASE_INSTATE_RECEIVING;
    pipebase->vfptr->recv (pipebase, buf, len);
    if (sp_fast (pipebase->instate == SP_PIPEBASE_INSTATE_RECEIVED)) {
        pipebase->instate = SP_PIPEBASE_INSTATE_IDLE;
        return 0;
    }
    sp_assert (pipebase->instate == SP_PIPEBASE_INSTATE_RECEIVING);
    pipebase->instate = SP_PIPEBASE_INSTATE_ASYNC;
    return SP_PIPEBASE_RELEASE;
}

