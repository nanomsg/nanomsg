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

#include "pull.h"

#include "../../sp.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"

struct sp_pull {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_pull_term (struct sp_sockbase *self);
static int sp_pull_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_pull_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pull_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pull_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pull_send (struct sp_sockbase *self, const void *buf,
    size_t len);
static int sp_pull_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_pull_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_pull_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_pull_sockbase_vfptr = {
    sp_pull_term,
    sp_pull_add,
    sp_pull_rm,
    sp_pull_in,
    sp_pull_out,
    sp_pull_send,
    sp_pull_recv,
    sp_pull_setopt,
    sp_pull_getopt
};

void sp_pull_init (struct sp_pull *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
}

void sp_pull_term (struct sp_sockbase *self)
{
    struct sp_pull *pull;

    pull = sp_cont (self, struct sp_pull, sockbase);

    sp_excl_term (&pull->excl);
}

static int sp_pull_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_pull, sockbase)->excl,
        pipe);
}

static void sp_pull_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_pull, sockbase)->excl, pipe);
}

static int sp_pull_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_pull, sockbase)->excl, pipe);
}

static int sp_pull_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_pull, sockbase)->excl, pipe);
}

static int sp_pull_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    /*  Pull socket cannot be used for sending. */
    return -EAGAIN;
}

static int sp_pull_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    return sp_excl_recv (&sp_cont (self, struct sp_pull, sockbase)->excl,
        buf, len);
}

static int sp_pull_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_pull_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_pull_create (int fd)
{
    struct sp_pull *self;

    self = sp_alloc (sizeof (struct sp_pull));
    alloc_assert (self);
    sp_pull_init (self, &sp_pull_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_pull_socktype_struct = {
    AF_SP,
    SP_PULL,
    sp_pull_create
};

struct sp_socktype *sp_pull_socktype = &sp_pull_socktype_struct;

