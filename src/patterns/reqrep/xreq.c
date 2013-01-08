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

#include "xreq.h"

#include "../../sp.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"

/*  Private functions. */
static void sp_xreq_destroy (struct sp_sockbase *self);

static const struct sp_sockbase_vfptr sp_xreq_sockbase_vfptr = {
    sp_xreq_destroy,
    sp_xreq_add,
    sp_xreq_rm,
    sp_xreq_in,
    sp_xreq_out,
    sp_xreq_send,
    sp_xreq_recv,
    sp_xreq_setopt,
    sp_xreq_getopt
};

void sp_xreq_init (struct sp_xreq *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
}

void sp_xreq_term (struct sp_xreq *self)
{
    sp_excl_term (&self->excl);
}

static void sp_xreq_destroy (struct sp_sockbase *self)
{
    struct sp_xreq *xreq;

    xreq = sp_cont (self, struct sp_xreq, sockbase);

    sp_xreq_term (xreq);
    sp_free (xreq);
}

int sp_xreq_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_xreq, sockbase)->excl, pipe);
}

void sp_xreq_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_xreq, sockbase)->excl, pipe);
}

int sp_xreq_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_xreq, sockbase)->excl, pipe);
}

int sp_xreq_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_xreq, sockbase)->excl, pipe);
}

int sp_xreq_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    return sp_excl_send (&sp_cont (self, struct sp_xreq, sockbase)->excl,
        buf, len);
}

int sp_xreq_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    return sp_excl_recv (&sp_cont (self, struct sp_xreq, sockbase)->excl,
        buf, len);
}

int sp_xreq_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int sp_xreq_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_xreq_create (int fd)
{
    struct sp_xreq *self;

    self = sp_alloc (sizeof (struct sp_xreq), "socket (xreq)");
    alloc_assert (self);
    sp_xreq_init (self, &sp_xreq_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xreq_socktype_struct = {
    AF_SP_RAW,
    SP_REQ,
    sp_xreq_create
};

struct sp_socktype *sp_xreq_socktype = &sp_xreq_socktype_struct;

