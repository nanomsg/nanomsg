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

#include "xpull.h"

#include "../../sp.h"
#include "../../fanout.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"

struct sp_xpull {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
};

/*  Private functions. */
static void sp_xpull_init (struct sp_xpull *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_xpull_term (struct sp_xpull *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_xpull_destroy (struct sp_sockbase *self);
static int sp_xpull_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_xpull_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpull_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpull_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpull_send (struct sp_sockbase *self, const void *buf,
    size_t len);
static int sp_xpull_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_xpull_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_xpull_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_xpull_sockbase_vfptr = {
    sp_xpull_destroy,
    sp_xpull_add,
    sp_xpull_rm,
    sp_xpull_in,
    sp_xpull_out,
    sp_xpull_send,
    sp_xpull_recv,
    sp_xpull_setopt,
    sp_xpull_getopt
};

static void sp_xpull_init (struct sp_xpull *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
}

static void sp_xpull_term (struct sp_xpull *self)
{
    sp_excl_term (&self->excl);
}

void sp_xpull_destroy (struct sp_sockbase *self)
{
    struct sp_xpull *xpull;

    xpull = sp_cont (self, struct sp_xpull, sockbase);

    sp_xpull_term (xpull);
    sp_free (xpull);
}

static int sp_xpull_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_xpull, sockbase)->excl,
        pipe);
}

static void sp_xpull_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_xpull, sockbase)->excl, pipe);
}

static int sp_xpull_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_xpull, sockbase)->excl, pipe);
}

static int sp_xpull_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_xpull, sockbase)->excl, pipe);
}

static int sp_xpull_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    return -ENOTSUP;
}

static int sp_xpull_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    return sp_excl_recv (&sp_cont (self, struct sp_xpull, sockbase)->excl,
        buf, len);
}

static int sp_xpull_setopt (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_xpull_getopt (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

struct sp_sockbase *sp_xpull_create (int fd)
{
    struct sp_xpull *self;

    self = sp_alloc (sizeof (struct sp_xpull), "socket (pull)");
    alloc_assert (self);
    sp_xpull_init (self, &sp_xpull_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xpull_socktype_struct = {
    AF_SP_RAW,
    SP_PULL,
    sp_xpull_create
};

struct sp_socktype *sp_xpull_socktype = &sp_xpull_socktype_struct;

