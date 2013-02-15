/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "xbus.h"

#include "../../nn.h"
#include "../../bus.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/dist.h"
#include "../../utils/fq.h"

struct nn_xbus_data {
    struct nn_pipe *pipe;
    struct nn_dist_data outitem;
    struct nn_fq_data initem;
};

struct nn_xbus {
    struct nn_sockbase sockbase;
    struct nn_dist outpipes;
    struct nn_fq inpipes;
};

/*  Private functions. */
static void nn_xbus_init (struct nn_xbus *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);
static void nn_xbus_term (struct nn_xbus *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xbus_destroy (struct nn_sockbase *self);
static int nn_xbus_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xbus_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xbus_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xbus_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xbus_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xbus_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xbus_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen);
static int nn_xbus_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen);
static int nn_xbus_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen);
static int nn_xbus_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen);
static const struct nn_sockbase_vfptr nn_xbus_sockbase_vfptr = {
    nn_xbus_destroy,
    nn_xbus_add,
    nn_xbus_rm,
    nn_xbus_in,
    nn_xbus_out,
    nn_xbus_send,
    nn_xbus_recv,
    nn_xbus_setopt,
    nn_xbus_getopt,
    nn_xbus_sethdr,
    nn_xbus_gethdr
};

static void nn_xbus_init (struct nn_xbus *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    nn_sockbase_init (&self->sockbase, vfptr, fd);
    nn_dist_init (&self->outpipes);
    nn_fq_init (&self->inpipes);
}

static void nn_xbus_term (struct nn_xbus *self)
{
    nn_fq_term (&self->inpipes);
    nn_dist_term (&self->outpipes);
    nn_sockbase_term (&self->sockbase);
}

void nn_xbus_destroy (struct nn_sockbase *self)
{
    struct nn_xbus *xbus;

    xbus = nn_cont (self, struct nn_xbus, sockbase);

    nn_xbus_term (xbus);
    nn_free (xbus);
}

static int nn_xbus_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xbus *xbus;
    struct nn_xbus_data *data;

    xbus = nn_cont (self, struct nn_xbus, sockbase);

    data = nn_alloc (sizeof (struct nn_xbus_data),
        "pipe data (xbus)");
    alloc_assert (data);
    data->pipe = pipe;
    nn_fq_add (&xbus->inpipes, pipe, &data->initem, 8);
    nn_dist_add (&xbus->outpipes, pipe, &data->outitem);
    nn_pipe_setdata (pipe, data);

    return 0;
}

static void nn_xbus_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xbus *xbus;
    struct nn_xbus_data *data;

    xbus = nn_cont (self, struct nn_xbus, sockbase);
    data = nn_pipe_getdata (pipe);

    nn_fq_rm (&xbus->inpipes, pipe, &data->initem);
    nn_dist_rm (&xbus->outpipes, pipe, &data->outitem);

    nn_free (data);
}

static int nn_xbus_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xbus *xbus;
    struct nn_xbus_data *data;

    xbus = nn_cont (self, struct nn_xbus, sockbase);
    data = nn_pipe_getdata (pipe);

    return nn_fq_in (&xbus->inpipes, pipe, &data->initem);
}

static int nn_xbus_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xbus *xbus;
    struct nn_xbus_data *data;

    xbus = nn_cont (self, struct nn_xbus, sockbase);
    data = nn_pipe_getdata (pipe);

    return nn_dist_out (&xbus->outpipes, pipe, &data->outitem);
}

static int nn_xbus_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_dist_send (&nn_cont (self, struct nn_xbus, sockbase)->outpipes,
        msg);
}

static int nn_xbus_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_fq_recv (&nn_cont (self, struct nn_xbus, sockbase)->inpipes,
        msg, NULL);
}

static int nn_xbus_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xbus_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xbus_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (nn_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int nn_xbus_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

struct nn_sockbase *nn_xbus_create (int fd)
{
    struct nn_xbus *self;

    self = nn_alloc (sizeof (struct nn_xbus), "socket (bus)");
    alloc_assert (self);
    nn_xbus_init (self, &nn_xbus_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct nn_socktype nn_xbus_socktype_struct = {
    AF_SP_RAW,
    NN_BUS,
    nn_xbus_create
};

struct nn_socktype *nn_xbus_socktype = &nn_xbus_socktype_struct;

