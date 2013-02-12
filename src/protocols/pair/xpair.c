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

#include "xpair.h"

#include "../../nn.h"
#include "../../pair.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"

struct nn_xpair {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
};

/*  Private functions. */
static void nn_xpair_init (struct nn_xpair *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);
static void nn_xpair_term (struct nn_xpair *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xpair_destroy (struct nn_sockbase *self);
static int nn_xpair_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xpair_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xpair_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xpair_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xpair_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xpair_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xpair_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen);
static int nn_xpair_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen);
static int nn_xpair_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen);
static int nn_xpair_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen);
static const struct nn_sockbase_vfptr nn_xpair_sockbase_vfptr = {
    nn_xpair_destroy,
    nn_xpair_add,
    nn_xpair_rm,
    nn_xpair_in,
    nn_xpair_out,
    nn_xpair_send,
    nn_xpair_recv,
    nn_xpair_setopt,
    nn_xpair_getopt,
    nn_xpair_sethdr,
    nn_xpair_gethdr
};

static void nn_xpair_init (struct nn_xpair *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    nn_sockbase_init (&self->sockbase, vfptr, fd);
    nn_excl_init (&self->excl);
}

static void nn_xpair_term (struct nn_xpair *self)
{
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
}

void nn_xpair_destroy (struct nn_sockbase *self)
{
    struct nn_xpair *xpair;

    xpair = nn_cont (self, struct nn_xpair, sockbase);

    nn_xpair_term (xpair);
    nn_free (xpair);
}

static int nn_xpair_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_add (&nn_cont (self, struct nn_xpair, sockbase)->excl, pipe);
}

static void nn_xpair_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_rm (&nn_cont (self, struct nn_xpair, sockbase)->excl, pipe);
}

static int nn_xpair_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_in (&nn_cont (self, struct nn_xpair, sockbase)->excl, pipe);
}

static int nn_xpair_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_out (&nn_cont (self, struct nn_xpair, sockbase)->excl, pipe);
}

static int nn_xpair_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_excl_send (&nn_cont (self, struct nn_xpair, sockbase)->excl,
        msg);
}

static int nn_xpair_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_excl_recv (&nn_cont (self, struct nn_xpair, sockbase)->excl, msg);
}

static int nn_xpair_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xpair_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xpair_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (nn_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int nn_xpair_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

struct nn_sockbase *nn_xpair_create (int fd)
{
    struct nn_xpair *self;

    self = nn_alloc (sizeof (struct nn_xpair), "socket (pair)");
    alloc_assert (self);
    nn_xpair_init (self, &nn_xpair_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct nn_socktype nn_xpair_socktype_struct = {
    AF_SP_RAW,
    NN_PAIR,
    nn_xpair_create
};

struct nn_socktype *nn_xpair_socktype = &nn_xpair_socktype_struct;

