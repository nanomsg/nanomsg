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

#include "inprocb.h"
#include "inproc_ctx.h"
#include "msgpipe.h"

#include "../../utils/err.h"
#include "../../utils/fast.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

#include <string.h>

/*  Implementation of sp_epbase interface. */
static int sp_inprocb_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_inprocb_epbase_vfptr =
    {sp_inprocb_close};

int sp_inprocb_init (struct sp_inprocb *self, const char *addr, void *hint)
{
    sp_epbase_init (&self->epbase, &sp_inprocb_epbase_vfptr, hint);

    /*  Store the name of the endpoint. */
    if (sp_slow (strlen (addr) + 1 > SP_INPROCB_NAMELEN_MAX))
        return -ENAMETOOLONG;
    strcpy (self->addr, addr);

    sp_list_init (&self->pipes);

    return 0;
}

void sp_inprocb_add_pipe (struct sp_inprocb *self, struct sp_msgpipe *pipe)
{
    sp_list_insert (&self->pipes, &pipe->inprocb, sp_list_end (&self->pipes));
}

static int sp_inprocb_close (struct sp_epbase *self, int linger)
{
    struct sp_inprocb *inprocb;
    struct sp_list_item *it;
    struct sp_list_item *old_it;

    inprocb = sp_cont (self, struct sp_inprocb, epbase);

    /*  Disconnect all the pipes from the bind-side of the socket. */
    it = sp_list_begin (&inprocb->pipes);
    while (it != sp_list_end (&inprocb->pipes)) {

        /*  The message pipe may be deallocated inside detachb function thus
            we have to get the next item in the list in advance. */
        old_it = it;
        it = sp_list_next (&inprocb->pipes, it);
        sp_msgpipe_detachb (sp_cont (old_it, struct sp_msgpipe, inprocb));
    }

    /*  Remove the endpoint from the repository of all inproc endpoints. */
    sp_inproc_ctx_unbind (inprocb);

    sp_epbase_term (&inprocb->epbase);
    sp_list_term (&inprocb->pipes);

    /*  We can deallocate the endpoint straight away. There is no delayed
        termination for inproc endpoints. */
    sp_free (inprocb);

    return 0;
}

