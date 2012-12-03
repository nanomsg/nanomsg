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

#include "source.h"

#include "../../sp.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"

struct sp_source {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_source_term (struct sp_sockbase *self);
static int sp_source_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_source_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_source_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_source_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_source_send (struct sp_sockbase *self, const void *buf,
    size_t len);
static int sp_source_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_source_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_source_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_source_sockbase_vfptr = {
    sp_source_term,
    sp_source_add,
    sp_source_rm,
    sp_source_in,
    sp_source_out,
    sp_source_send,
    sp_source_recv,
    sp_source_setopt,
    sp_source_getopt
};

void sp_source_init (struct sp_source *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
}

void sp_source_term (struct sp_sockbase *self)
{
    struct sp_source *source;

    source = sp_cont (self, struct sp_source, sockbase);

    sp_excl_term (&source->excl);
}

static int sp_source_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_source, sockbase)->excl,
        pipe);
}

static void sp_source_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_source, sockbase)->excl, pipe);
}

static int sp_source_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_source, sockbase)->excl, pipe);
}

static int sp_source_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_source, sockbase)->excl, pipe);
}

static int sp_source_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    return sp_excl_send (&sp_cont (self, struct sp_source, sockbase)->excl,
        buf, len);
}

static int sp_source_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    /*  No messages can be received from source socket. */
    return -EAGAIN;
}

static int sp_source_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_source_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_source_create (int fd)
{
    struct sp_source *self;

    self = sp_alloc (sizeof (struct sp_source));
    alloc_assert (self);
    sp_source_init (self, &sp_source_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_source_socktype_struct = {
    AF_SP,
    SP_SOURCE,
    sp_source_create
};

struct sp_socktype *sp_source_socktype = &sp_source_socktype_struct;

