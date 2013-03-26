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

#include "inprocb.h"
#include "inproc_ctx.h"
#include "msgpipe.h"

#include "../../utils/err.h"
#include "../../utils/fast.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <string.h>

/*  Implementation of nn_epbase interface. */
static int nn_inprocb_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_inprocb_epbase_vfptr =
    {nn_inprocb_close};

int nn_inprocb_init (struct nn_inprocb *self, const char *addr, void *hint)
{
    nn_epbase_init (&self->epbase, &nn_inprocb_epbase_vfptr, addr, hint);
    nn_list_item_init (&self->list);
    nn_list_init (&self->pipes);
    self->flags = 0;

    return 0;
}

const char *nn_inprocb_getaddr (struct nn_inprocb *self)
{
    return nn_epbase_getaddr (&self->epbase);
}

int nn_inprocb_socktype (struct nn_inprocb *self)
{
    int socktype;
    size_t sz;

    sz = sizeof (socktype);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
        &socktype, &sz);
    nn_assert (sz == sizeof (socktype));
    return socktype;
}

int nn_inprocb_ispeer (struct nn_inprocb *self, int socktype)
{
    return nn_epbase_ispeer (&self->epbase, socktype);
}

void nn_inprocb_add_pipe (struct nn_inprocb *self, struct nn_msgpipe *pipe)
{
    nn_assert (!(self->flags & NN_INPROCB_FLAG_TERMINATING));
    nn_list_insert (&self->pipes, &pipe->item, nn_list_end (&self->pipes));
}

void nn_inprocb_rm_pipe (struct nn_inprocb *self, struct nn_msgpipe *pipe)
{
    nn_list_erase (&self->pipes, &pipe->item);

    if (self->flags & NN_INPROCB_FLAG_TERMINATING &&
          nn_list_empty (&self->pipes)) {
        nn_list_term (&self->pipes);
        nn_epbase_term (&self->epbase);
        nn_free (self);
        return;
    }
}

static int nn_inprocb_close (struct nn_epbase *self)
{
    struct nn_inprocb *inprocb;
    struct nn_list_item *it;
    struct nn_list_item *nextit;

    inprocb = nn_cont (self, struct nn_inprocb, epbase);

    /*  Remove the endpoint from the repository of all inproc endpoints.
        No new connections to this endpoint may be created from now on. */
    nn_inproc_ctx_unbind (inprocb);

    /*  Disconnect all the pipes from the bind-side of the socket. The message
        pipe may be deallocated inside detachb function thus we have to get
        the next item in the list in advance.*/
    it = nn_list_begin (&inprocb->pipes);
    while (it != nn_list_end (&inprocb->pipes)) {
        nextit = nn_list_next (&inprocb->pipes, it);
        nn_msgpipe_detachb (nn_cont (it, struct nn_msgpipe, item));
        it = nextit;
    }

    /*  Remember that close was already called. Later on, when all the pipes
        detach from this object, it can be deallocated. */
    inprocb->flags |= NN_INPROCB_FLAG_TERMINATING;

    /*  If there's no pipe attached, deallocate the object straight away. */
    if (nn_list_empty (&inprocb->pipes)) {
        nn_list_term (&inprocb->pipes);
        nn_list_item_term (&inprocb->list);
        nn_epbase_term (&inprocb->epbase);
        nn_free (inprocb);
        return 0;
    }

    return -EINPROGRESS;
}

