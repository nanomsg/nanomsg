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

#include "xsink.h"

#include "../../nn.h"
#include "../../fanin.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/fq.h"
#include "../../utils/list.h"

struct nn_xsink_data {
    struct nn_fq_data fq;
};

struct nn_xsink {
    struct nn_sockbase sockbase;
    struct nn_fq fq;
};

/*  Private functions. */
static int nn_xsink_init (struct nn_xsink *self,
    const struct nn_sockbase_vfptr *vfptr);
static void nn_xsink_term (struct nn_xsink *self);

/*  Implementation of nn_sockbase's virtual functions. */
static int nn_xsink_ispeer (int socktype);
static void nn_xsink_destroy (struct nn_sockbase *self);
static int nn_xsink_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsink_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsink_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsink_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xsink_events (struct nn_sockbase *self);
static int nn_xsink_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsink_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_xsink_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xsink_sockbase_vfptr = {
    NN_SOCKBASE_FLAG_NOSEND,
    nn_xsink_ispeer,
    nn_xsink_destroy,
    nn_xsink_add,
    nn_xsink_rm,
    nn_xsink_in,
    nn_xsink_out,
    nn_xsink_events,
    NULL,
    nn_xsink_recv,
    nn_xsink_setopt,
    nn_xsink_getopt
};

static int nn_xsink_ispeer (int socktype)
{
    return socktype == NN_SOURCE ? 1 : 0;
}

static int nn_xsink_init (struct nn_xsink *self,
    const struct nn_sockbase_vfptr *vfptr)
{
    int rc;

    rc = nn_sockbase_init (&self->sockbase, vfptr);
    if (rc < 0)
        return rc;

    nn_fq_init (&self->fq);

    return 0;
}

static void nn_xsink_term (struct nn_xsink *self)
{
    nn_fq_term (&self->fq);
    nn_sockbase_term (&self->sockbase);
}

void nn_xsink_destroy (struct nn_sockbase *self)
{
    struct nn_xsink *xsink;

    xsink = nn_cont (self, struct nn_xsink, sockbase);

    nn_xsink_term (xsink);
    nn_free (xsink);
}

static int nn_xsink_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsink *xsink;
    struct nn_xsink_data *data;

    xsink = nn_cont (self, struct nn_xsink, sockbase);
    data = nn_alloc (sizeof (struct nn_xsink_data), "pipe data (sink)");
    alloc_assert (data);
    nn_pipe_setdata (pipe, data);
    nn_fq_add (&xsink->fq, pipe, &data->fq, self->rcvprio);

    return 0;
}

static void nn_xsink_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsink *xsink;
    struct nn_xsink_data *data;

    xsink = nn_cont (self, struct nn_xsink, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_fq_rm (&xsink->fq, pipe, &data->fq);
    nn_free (data);
}

static void nn_xsink_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsink *xsink;
    struct nn_xsink_data *data;

    xsink = nn_cont (self, struct nn_xsink, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_fq_in (&xsink->fq, pipe, &data->fq);
}

static void nn_xsink_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    /*  We are not going to send any messages, so there's no need to store
        the list of outbound pipes. */
}

static int nn_xsink_events (struct nn_sockbase *self)
{
    return nn_fq_can_recv (&nn_cont (self, struct nn_xsink, sockbase)->fq) ?
        NN_SOCKBASE_EVENT_IN : 0;
}

static int nn_xsink_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;

    rc = nn_fq_recv (&nn_cont (self, struct nn_xsink, sockbase)->fq,
        msg, NULL);

    /*  Discard NN_PIPEBASE_PARSED flag. */
    return rc < 0 ? rc : 0;
}

static int nn_xsink_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xsink_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xsink_create (struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_xsink *self;

    self = nn_alloc (sizeof (struct nn_xsink), "socket (sink)");
    alloc_assert (self);
    rc = nn_xsink_init (self, &nn_xsink_sockbase_vfptr);
    if (rc < 0) {
        nn_free (self);
        return rc;
    }
    *sockbase = &self->sockbase;

    return 0;
}

static struct nn_socktype nn_xsink_socktype_struct = {
    AF_SP_RAW,
    NN_SINK,
    nn_xsink_create,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xsink_socktype = &nn_xsink_socktype_struct;

