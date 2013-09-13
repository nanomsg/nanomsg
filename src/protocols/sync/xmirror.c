/*
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#include "xmirror.h"

#include "../../nn.h"
#include "../../sync.h"

#include "../utils/excl.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"

struct nn_xmirror {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
};

/*  Private functions. */
static void nn_xmirror_init (struct nn_xmirror *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xmirror_term (struct nn_xmirror *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xmirror_destroy (struct nn_sockbase *self);
static int nn_xmirror_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmirror_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmirror_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmirror_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xmirror_events (struct nn_sockbase *self);
static int nn_xmirror_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xmirror_setopt (struct nn_sockbase *self, int level,
    int option, const void *optval, size_t optvallen);
static int nn_xmirror_getopt (struct nn_sockbase *self, int level,
    int option, void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xmirror_sockbase_vfptr = {
    NULL,
    nn_xmirror_destroy,
    nn_xmirror_add,
    nn_xmirror_rm,
    nn_xmirror_in,
    nn_xmirror_out,
    nn_xmirror_events,
    NULL,
    nn_xmirror_recv,
    nn_xmirror_setopt,
    nn_xmirror_getopt
};

static void nn_xmirror_init (struct nn_xmirror *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_excl_init (&self->excl);
}

static void nn_xmirror_term (struct nn_xmirror *self)
{
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
}

void nn_xmirror_destroy (struct nn_sockbase *self)
{
    struct nn_xmirror *xmirror;

    xmirror = nn_cont (self, struct nn_xmirror, sockbase);

    nn_xmirror_term (xmirror);
    nn_free (xmirror);
}

static int nn_xmirror_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xmirror *xmirror;

    xmirror = nn_cont (self, struct nn_xmirror, sockbase);

    nn_excl_add (&xmirror->excl, pipe);

    return 0;
}

static void nn_xmirror_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xmirror *xmirror;

    xmirror = nn_cont (self, struct nn_xmirror, sockbase);

    nn_excl_rm (&xmirror->excl, pipe);
}

static void nn_xmirror_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xmirror *xmirror;

    xmirror = nn_cont (self, struct nn_xmirror, sockbase);

    nn_excl_in (&xmirror->excl, pipe);
}

static void nn_xmirror_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    /*  We are not going to send any messages, so there's no point is
        maintaining a list of pipes ready for sending. */
}

static int nn_xmirror_events (struct nn_sockbase *self)
{
    return nn_excl_can_recv (&nn_cont (self, struct nn_xmirror,
        sockbase)->excl) ? NN_SOCKBASE_EVENT_IN : 0;
}

static int nn_xmirror_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;

    rc = nn_excl_recv (&nn_cont (self, struct nn_xmirror, sockbase)->excl,
         msg);

    /*  Discard NN_PIPEBASE_PARSED flag. */
    return rc < 0 ? rc : 0;
}

static int nn_xmirror_setopt (struct nn_sockbase *self, int level,
    int option, const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xmirror_getopt (struct nn_sockbase *self, int level,
    int option, void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xmirror_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xmirror *self;

    self = nn_alloc (sizeof (struct nn_xmirror), "socket (mirror)");
    alloc_assert (self);
    nn_xmirror_init (self, &nn_xmirror_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xmirror_ispeer (int socktype)
{
    return socktype == NN_MASTER ? 1 : 0;
}

static struct nn_socktype nn_xmirror_socktype_struct = {
    AF_SP_RAW,
    NN_MIRROR,
    NN_SOCKTYPE_FLAG_NOSEND,
    nn_xmirror_create,
    nn_xmirror_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xmirror_socktype = &nn_xmirror_socktype_struct;

