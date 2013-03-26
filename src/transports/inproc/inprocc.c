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

#include "inprocc.h"
#include "inproc_ctx.h"

#include "../../utils/err.h"
#include "../../utils/fast.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <string.h>
#include <stddef.h>

/*  Implementation of nn_epbase callback interface. */
static int nn_inprocc_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_inprocc_epbase_vfptr =
    {nn_inprocc_close};

int nn_inprocc_init (struct nn_inprocc *self, const char *addr, void *hint)
{
    nn_epbase_init (&self->epbase, &nn_inprocc_epbase_vfptr, addr, hint);
    nn_list_item_init (&self->list);
    self->flags = 0;

    /*  The endpoint is not connected at the moment. */
    self->pipe = NULL;

    return 0;
}

const char *nn_inprocc_getaddr (struct nn_inprocc *self)
{
    return nn_epbase_getaddr (&self->epbase);
}

int nn_inprocc_socktype (struct nn_inprocc *self)
{
    int socktype;
    size_t sz;

    sz = sizeof (socktype);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
        &socktype, &sz);
    nn_assert (sz == sizeof (socktype));
    return socktype;
}

int nn_inprocc_ispeer (struct nn_inprocc *self, int socktype)
{
    return nn_epbase_ispeer (&self->epbase, socktype);
}

void nn_inprocc_add_pipe (struct nn_inprocc *self, struct nn_msgpipe *pipe)
{
    /*  This should never happen. If the endpoint is already connected it
        shouldn't get connected anew. */
    nn_assert (!self->pipe);

    /*  If already terminating, no new pipe should be attched to the object. */
    nn_assert (!(self->flags & NN_INPROCC_FLAG_TERMINATING));

    /*  Store the reference to the pipe. */
    self->pipe = pipe;
}

void nn_inprocc_rm_pipe (struct nn_inprocc *self, struct nn_msgpipe *pipe)
{
    /*  Make sure that we are removing the currently attached pipe. */
    nn_assert (self->pipe && self->pipe == pipe);

    self->pipe = NULL;
    if (self->flags & NN_INPROCC_FLAG_TERMINATING) {
        nn_epbase_term (&self->epbase);
        nn_free (self);
        return;
    }
}

static int nn_inprocc_close (struct nn_epbase *self)
{
    struct nn_inprocc *inprocc;

    inprocc = nn_cont (self, struct nn_inprocc, epbase);

    /*  Remove the endpoint from the repository of all inproc endpoints.
        From now on no new pipes should be attached to this object. */
    nn_inproc_ctx_disconnect (inprocc);

    /*  If the endpoint is connected, detach the pipe from the connect-side
        socket. The pipe may be deallocated in the process, so make sure that
        the pointer won't be used any more. */
    if (inprocc->pipe) {

        /*  Remember that close was already called. Later on, when the pipe
            detaches from this object, it can be deallocated. */
        inprocc->flags |= NN_INPROCC_FLAG_TERMINATING;
        nn_msgpipe_detachc (inprocc->pipe);
        return -EINPROGRESS;
    }

    nn_list_item_term (&inprocc->list);
    /*  If the endpoint is not connected we can deallocate it straight away. */
    nn_epbase_term (&inprocc->epbase);
    nn_free (inprocc);

    return 0;
}

