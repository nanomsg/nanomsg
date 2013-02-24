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

struct nn_xsource {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
};

/*  Private functions. */
static int nn_xsource_init (struct nn_xsource *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);
static void nn_xsource_term (struct nn_xsource *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_xsource_destroy (struct nn_sockbase *self);
static int nn_xsource_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_xsource_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_xsource_events (struct nn_sockbase *self);
static int nn_xsource_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsource_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_xsource_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_xsource_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static int nn_xsource_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen);
static int nn_xsource_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen);
static const struct nn_sockbase_vfptr nn_xsource_sockbase_vfptr = {
    nn_xsource_destroy,
    nn_xsource_add,
    nn_xsource_rm,
    nn_xsource_in,
    nn_xsource_out,
    nn_xsource_events,
    nn_xsource_send,
    nn_xsource_recv,
    nn_xsource_setopt,
    nn_xsource_getopt,
    nn_xsource_sethdr,
    nn_xsource_gethdr
};

static int nn_xsource_init (struct nn_xsource *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    int rc;

    rc = nn_sockbase_init (&self->sockbase, vfptr, fd);
    if (rc < 0)
        return rc;

    nn_excl_init (&self->excl);

    return 0;
}

static void nn_xsource_term (struct nn_xsource *self)
{
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
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
    struct nn_xsource *xsource;
    int events;

    xsource = nn_cont (self, struct nn_xsource, sockbase);

    events = 0;
    if (nn_excl_can_recv (&xsource->excl))
        events |= NN_SOCKBASE_EVENT_IN;
    if (nn_excl_can_send (&xsource->excl))
        events |= NN_SOCKBASE_EVENT_OUT;
    return events;
}

static int nn_xsource_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    return nn_excl_send (&nn_cont (self, struct nn_xsource, sockbase)->excl,
        msg);
}

static int nn_xsource_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    return -ENOTSUP;
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

static int nn_xsource_sethdr (struct nn_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (nn_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int nn_xsource_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

int nn_xsource_create (int fd, struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_xsource *self;

    self = nn_alloc (sizeof (struct nn_xsource), "socket (source)");
    alloc_assert (self);
    rc = nn_xsource_init (self, &nn_xsource_sockbase_vfptr, fd);
    if (rc < 0) {
        nn_free (self);
        return rc;
    }
    *sockbase = &self->sockbase;

    return 0;
}

static struct nn_socktype nn_xsource_socktype_struct = {
    AF_SP_RAW,
    NN_SOURCE,
    nn_xsource_create
};

struct nn_socktype *nn_xsource_socktype = &nn_xsource_socktype_struct;

