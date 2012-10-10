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

#include "../transport.h"
#include "../pattern.h"

#include "../utils/err.h"

int sp_pipebase_init (struct sp_pipebase *self,
    const struct sp_pipebase_vfptr *vfptr, struct sp_epbase *epbase)
{
    sp_assert (epbase->sock);
    self->vfptr = vfptr;
    self->epbase = epbase;
    return sp_sock_add (self->epbase->sock, self);
}

void sp_pipebase_term (struct sp_pipebase *self)
{
    if (self->epbase->sock)
        sp_sock_rm (self->epbase->sock, self);
}

void sp_pipebase_in (struct sp_pipebase *self)
{
    if (self->epbase->sock)
        sp_sock_in (self->epbase->sock, self);
}

void sp_pipebase_out (struct sp_pipebase *self)
{
    if (self->epbase->sock)
        sp_sock_out (self->epbase->sock, self);
}

void sp_pipe_setdata (struct sp_pipe *self, void *data)
{
    ((struct sp_pipebase*) self)->data = data;
}

void *sp_pipe_getdata (struct sp_pipe *self)
{
    return ((struct sp_pipebase*) self)->data;
}

int sp_pipe_send (struct sp_pipe *self, const void *buf, size_t len)
{
    struct sp_pipebase *pipebase;

    pipebase = (struct sp_pipebase*) self;
    return pipebase->vfptr->send (pipebase, buf, len);
}

int sp_pipe_recv (struct sp_pipe *self, void *buf, size_t *len)
{
    struct sp_pipebase *pipebase;

    pipebase = (struct sp_pipebase*) self;
    return pipebase->vfptr->recv (pipebase, buf, len);
}

