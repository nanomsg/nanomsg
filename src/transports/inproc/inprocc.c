/*
    Copyright (c) 2012 250bpm s.r.o.

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
static void nn_inprocc_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_inprocc_epbase_vfptr =
    {nn_inprocc_close};

int nn_inprocc_init (struct nn_inprocc *self, const char *addr, void *hint)
{
    nn_epbase_init (&self->epbase, &nn_inprocc_epbase_vfptr, addr, hint);

    /*  Store the name of the endpoint. */
    if (nn_slow (strlen (addr) + 1 > NN_INPROCC_NAMELEN_MAX))
        return -ENAMETOOLONG;
#if defined _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif
    strcpy (self->addr, addr);
#if defined _MSC_VER
#pragma warning(pop)
#endif

    /*  The endpoint is not connected at the moment. */
    self->pipe = NULL;

    return 0;
}

void nn_inprocc_add_pipe (struct nn_inprocc *self, struct nn_msgpipe *pipe)
{
    /*  This should never happen. If the endpoint is already connected it
        shouldn't get connected anew. */
    nn_assert (!self->pipe);

    /*  Store the reference to the pipe. */
    self->pipe = pipe;
}

static void nn_inprocc_close (struct nn_epbase *self)
{
    struct nn_inprocc *inprocc;

    inprocc = nn_cont (self, struct nn_inprocc, epbase);

    /*  If the endpoint is connected, detach the pipe from the connect-side
        socket. */
    if (inprocc->pipe)
        nn_msgpipe_detachc (inprocc->pipe);

    /*  Remove the endpoint from the repository of all inproc endpoints. */
    nn_inproc_ctx_disconnect (inprocc);

    nn_epbase_term (&inprocc->epbase);

    /*  We can deallocate the endpoint straight away. There is no delayed
        termination for inproc endpoints. */
    nn_free (inprocc);
}

