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

#include "xsource.h"

#include "../../nn.h"
#include "../../fanin.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"
#include "../../utils/list.h"

struct nn_xsource {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
};

/*  Private functions. */
static void nn_xsource_init (struct nn_xsource *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xsource_term (struct nn_xsource *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xsource_stop (struct nn_sockbase *self);
static void nn_xsource_destroy (struct nn_sockbase *self);
static int nn_xsource_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xsource_events (struct nn_sockbase *self);
static int nn_xsource_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsource_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_xsource_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xsource_sockbase_vfptr = {
    nn_xsource_stop,
    nn_xsource_destroy,
    nn_xsource_add,
    nn_xsource_rm,
    nn_xsource_in,
    nn_xsource_out,
    nn_xsource_events,
    nn_xsource_send,
    NULL,
    nn_xsource_setopt,
    nn_xsource_getopt
};

static void nn_xsource_init (struct nn_xsource *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_excl_init (&self->excl);
}

static void nn_xsource_term (struct nn_xsource *self)
{
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
}

void nn_xsource_stop (struct nn_sockbase *self)
{
    /*  Nothing special to do done. The object is closed straight away. */
    nn_sockbase_stopped (self); 
}

void nn_xsource_destroy (struct nn_sockbase *self)
{
    struct nn_xsource *xsource;

    xsource = nn_cont (self, struct nn_xsource, sockbase);

    nn_xsource_term (xsource);
    nn_free (xsource);
}

static int nn_xsource_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_add (&nn_cont (self, struct nn_xsource, sockbase)->excl,
        pipe);
}

static void nn_xsource_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_rm (&nn_cont (self, struct nn_xsource, sockbase)->excl, pipe);
}

static void nn_xsource_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_in (&nn_cont (self, struct nn_xsource, sockbase)->excl, pipe);
}

static void nn_xsource_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_out (&nn_cont (self, struct nn_xsource, sockbase)->excl, pipe);
}

static int nn_xsource_events (struct nn_sockbase *self)
{
    return nn_excl_can_send (&nn_cont (self, struct nn_xsource,
        sockbase)->excl) ? NN_SOCKBASE_EVENT_OUT : 0;
}

static int nn_xsource_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_excl_send (&nn_cont (self, struct nn_xsource, sockbase)->excl,
        msg);
}

static int nn_xsource_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xsource_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xsource_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xsource *self;

    self = nn_alloc (sizeof (struct nn_xsource), "socket (source)");
    alloc_assert (self);
    nn_xsource_init (self, &nn_xsource_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xsource_ispeer (int socktype)
{
    return socktype == NN_SINK ? 1 : 0;
}

static struct nn_socktype nn_xsource_socktype_struct = {
    AF_SP_RAW,
    NN_SOURCE,
    NN_SOCKTYPE_FLAG_NORECV,
    nn_xsource_create,
    nn_xsource_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xsource_socktype = &nn_xsource_socktype_struct;

