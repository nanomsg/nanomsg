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

#include "xmaster.h"

#include "../../nn.h"
#include "../../sync.h"

#include "../utils/dist.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/msg.h"

#include <string.h>

struct nn_xmaster_data {
    struct nn_dist_data dist;

    /*  If set to 1 it represents a new connection where the state haven't
        been sent yet. 0 otherwise. */
    int fresh;
};

struct nn_xmaster {
    struct nn_sockbase sockbase;
    struct nn_dist dist;
    struct nn_msg cache;
};

/*  Private functions. */
static void nn_xmaster_init (struct nn_xmaster *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_xmaster_term (struct nn_xmaster *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xmaster_destroy (struct nn_sockbase *self);
static int nn_xmaster_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmaster_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmaster_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xmaster_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xmaster_events (struct nn_sockbase *self);
static int nn_xmaster_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xmaster_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_xmaster_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_xmaster_sockbase_vfptr = {
    NULL,
    nn_xmaster_destroy,
    nn_xmaster_add,
    nn_xmaster_rm,
    nn_xmaster_in,
    nn_xmaster_out,
    nn_xmaster_events,
    nn_xmaster_send,
    NULL,
    nn_xmaster_setopt,
    nn_xmaster_getopt
};

static void nn_xmaster_init (struct nn_xmaster *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_dist_init (&self->dist);
    nn_msg_init (&self->cache, 0);
}

static void nn_xmaster_term (struct nn_xmaster *self)
{
    nn_msg_term (&self->cache);
    nn_dist_term (&self->dist);
    nn_sockbase_term (&self->sockbase);
}

void nn_xmaster_destroy (struct nn_sockbase *self)
{
    struct nn_xmaster *xmaster;

    xmaster = nn_cont (self, struct nn_xmaster, sockbase);

    nn_xmaster_term (xmaster);
    nn_free (xmaster);
}

static int nn_xmaster_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_xmaster *xmaster;
    struct nn_xmaster_data *data;
    int sndprio;
    size_t sz;

    xmaster = nn_cont (self, struct nn_xmaster, sockbase);

    data = nn_alloc (sizeof (struct nn_xmaster_data), "pipe data (master)");
    alloc_assert (data);
    data->fresh = 1;
    nn_pipe_setdata (pipe, data);
    nn_dist_add (&xmaster->dist, pipe, &data->dist);

    return 0;
}

static void nn_xmaster_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xmaster *xmaster;
    struct nn_xmaster_data *data;

    xmaster = nn_cont (self, struct nn_xmaster, sockbase);
    data = nn_pipe_getdata (pipe);
    nn_dist_rm (&xmaster->dist, pipe, &data->dist);
    nn_free (data);
}

static void nn_xmaster_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    /*  We are not going to receive any messages, so there's no need to store
        the list of inbound pipes. */
}

static void nn_xmaster_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    int rc;
    struct nn_xmaster *xmaster;
    struct nn_xmaster_data *data;

    xmaster = nn_cont (self, struct nn_xmaster, sockbase);
    data = nn_pipe_getdata (pipe);

    /*  Send the state to newly established pipe. */
    if (data->fresh) {
        rc = nn_pipe_send (pipe, &xmaster->cache);
        errnum_assert (rc >= 0, -rc);
        data->fresh = 0;
        if (rc & NN_PIPE_RELEASE)
            return;
    }

    nn_dist_out (&xmaster->dist, pipe, &data->dist);
}

static int nn_xmaster_events (struct nn_sockbase *self)
{
    return NN_SOCKBASE_EVENT_OUT;
}

static int nn_xmaster_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    struct nn_xmaster *xmaster;

    xmaster = nn_cont (self, struct nn_xmaster, sockbase);

    /*  Check whether new state is the same as the old one.
        If so, do nothing. */
    if (nn_chunkref_size (&xmaster->cache.body) == nn_chunkref_size (&msg->body)
          && memcmp (nn_chunkref_data (&xmaster->cache.body),
          nn_chunkref_data (&msg->body), nn_chunkref_size (&msg->body)) == 0)
        return 0;

    /*  Overwrite the cache by the new state. */
    nn_msg_term (&xmaster->cache);
    nn_msg_cp (&xmaster->cache, msg);

    /*  Distribute the new state to all the mirrors. */
    return nn_dist_send (&xmaster->dist, msg, NULL);
}

static int nn_xmaster_setopt (struct nn_sockbase *self, int level,
    int option, const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_xmaster_getopt (struct nn_sockbase *self, int level,
    int option, void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xmaster_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_xmaster *self;

    self = nn_alloc (sizeof (struct nn_xmaster), "socket (master)");
    alloc_assert (self);
    nn_xmaster_init (self, &nn_xmaster_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

int nn_xmaster_ispeer (int socktype)
{
    return socktype == NN_MIRROR ? 1 : 0;
}

static struct nn_socktype nn_xmaster_socktype_struct = {
    AF_SP_RAW,
    NN_MASTER,
    NN_SOCKTYPE_FLAG_NORECV,
    nn_xmaster_create,
    nn_xmaster_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_xmaster_socktype = &nn_xmaster_socktype_struct;

