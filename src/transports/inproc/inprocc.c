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

/*  Implementation of sp_epbase callback interface. */
static int sp_inprocc_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_inprocc_epbase_vfptr =
    {sp_inprocc_close};

int sp_inprocc_init (struct sp_inprocc *self, const char *addr, void *hint)
{
    sp_epbase_init (&self->epbase, &sp_inprocc_epbase_vfptr, hint);

    /*  Store the name of the endpoint. */
    if (sp_slow (strlen (addr) + 1 > SP_INPROCC_NAMELEN_MAX))
        return -ENAMETOOLONG;
    strcpy (self->addr, addr);

    /*  The endpoint is not connected at the moment. */
    self->pipe = NULL;

    return 0;
}

void sp_inprocc_add_pipe (struct sp_inprocc *self, struct sp_msgpipe *pipe)
{
    /*  This should never happen. If the endpoint is already connected it
        shouldn't get connected anew. */
    sp_assert (!self->pipe);

    /*  Store the reference to the pipe. */
    self->pipe = pipe;
}

static int sp_inprocc_close (struct sp_epbase *self, int linger)
{
    struct sp_inprocc *inprocc;

    inprocc = sp_cont (self, struct sp_inprocc, epbase);

    /*  If the endpoint is connected, detach the pipe from the connect-side
        socket. */
    if (inprocc->pipe)
        sp_msgpipe_detachc (inprocc->pipe);

    /*  Remove the endpoint from the repository of all inproc endpoints. */
    sp_inproc_ctx_disconnect (inprocc);

    sp_epbase_term (&inprocc->epbase);

    /*  We can deallocate the endpoint straight away. There is no delayed
        termination for inproc endpoints. */
    sp_free (inprocc);

    return 0;
}

