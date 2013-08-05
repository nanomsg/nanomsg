/*
    Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.

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

#include "pub.h"

#include "../../nn.h"
#include "../../pubsub.h"

#include "../utils/dist.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"

#include <stddef.h>

struct nn_pub_data {
    struct nn_dist_data item;
};

struct nn_pub {

    /*  The generic socket base class. */
    struct nn_sockbase sockbase;

    /*  Distributor. */
    struct nn_dist outpipes;
};

/*  Private functions. */
static void nn_pub_init (struct nn_pub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_pub_term (struct nn_pub *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_pub_destroy (struct nn_sockbase *self);
static int nn_pub_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_pub_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_pub_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_pub_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_pub_events (struct nn_sockbase *self);
static int nn_pub_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_pub_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_pub_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_pub_sockbase_vfptr = {
    NULL,
    nn_pub_destroy,
    nn_pub_add,
    nn_pub_rm,
    nn_pub_in,
    nn_pub_out,
    nn_pub_events,
    nn_pub_send,
    NULL,
    nn_pub_setopt,
    nn_pub_getopt
};

static void nn_pub_init (struct nn_pub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_dist_init (&self->outpipes);
}

static void nn_pub_term (struct nn_pub *self)
{
    nn_dist_term (&self->outpipes);
    nn_sockbase_term (&self->sockbase);
}

void nn_pub_destroy (struct nn_sockbase *self)
{
    struct nn_pub *pub;

    pub = nn_cont (self, struct nn_pub, sockbase);

    nn_pub_term (pub);
    nn_free (pub);
}

static int nn_pub_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_pub *pub;
    struct nn_pub_data *data;

    pub = nn_cont (self, struct nn_pub, sockbase);

    data = nn_alloc (sizeof (struct nn_pub_data), "pipe data (pub)");
    alloc_assert (data);
    nn_dist_add (&pub->outpipes, pipe, &data->item);
    nn_pipe_setdata (pipe, data);

    return 0;
}

static void nn_pub_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_pub *pub;
    struct nn_pub_data *data;

    pub = nn_cont (self, struct nn_pub, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_dist_rm (&pub->outpipes, pipe, &data->item);

    nn_free (data);
}

static void nn_pub_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    /*  We shouldn't get any messages from subscribers. */
    nn_assert (0);
}

static void nn_pub_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_pub *pub;
    struct nn_pub_data *data;

    pub = nn_cont (self, struct nn_pub, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_dist_out (&pub->outpipes, pipe, &data->item);
}

static int nn_pub_events (struct nn_sockbase *self)
{
    return NN_SOCKBASE_EVENT_OUT;
}

static int nn_pub_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_dist_send (&nn_cont (self, struct nn_pub, sockbase)->outpipes,
        msg, NULL);
}

static int nn_pub_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_pub_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_pub_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_pub *self;

    self = nn_alloc (sizeof (struct nn_pub), "socket (pub)");
    alloc_assert (self);
    nn_pub_init (self, &nn_pub_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

static int nn_pub_ispeer (int socktype)
{
     return socktype == NN_SUB ? 1 : 0;
}

static struct nn_socktype nn_pub_socktype_struct = {
    AF_SP,
    NN_PUB,
    NN_SOCKTYPE_FLAG_NORECV,
    nn_pub_create,
    nn_pub_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_pub_socktype = &nn_pub_socktype_struct;

