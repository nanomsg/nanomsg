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

#include "xsurveyor.h"
#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

#include <stddef.h>

/*  Implementation of sp_sockbase's virtual functions. */
static const struct sp_sockbase_vfptr sp_xsurveyor_sockbase_vfptr = {
    sp_xsurveyor_term,
    sp_xsurveyor_add,
    sp_xsurveyor_rm,
    sp_xsurveyor_in,
    sp_xsurveyor_out,
    sp_xsurveyor_send,
    sp_xsurveyor_recv,
    sp_xsurveyor_setopt,
    sp_xsurveyor_getopt
};

void sp_xsurveyor_init (struct sp_xsurveyor *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->outpipes);
    sp_list_init (&self->inpipes);
    self->current = NULL;
}

void sp_xsurveyor_term (struct sp_sockbase *self)
{
    struct sp_xsurveyor *xsurveyor;

    xsurveyor = sp_cont (self, struct sp_xsurveyor, sockbase);

    sp_list_term (&xsurveyor->inpipes);
    sp_list_term (&xsurveyor->outpipes);
}

int sp_xsurveyor_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsurveyor_data *data;

    data = sp_alloc (sizeof (struct sp_xsurveyor_data),
        "pipe data (xsurveyor)");
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

void sp_xsurveyor_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsurveyor_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in the pipe lists, remove it. */

    sp_free (data);
}

int sp_xsurveyor_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsurveyor *xsurveyor;
    struct sp_xsurveyor_data *data;
    int result;

    xsurveyor = sp_cont (self, struct sp_xsurveyor, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&xsurveyor->inpipes) ? 1 : 0;
    if (result)
        xsurveyor->current = data;
    sp_list_insert (&xsurveyor->inpipes, &data->initem,
        sp_list_end (&xsurveyor->inpipes));
    return result;
}

int sp_xsurveyor_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsurveyor *xsurveyor;
    struct sp_xsurveyor_data *data;
    int result;

    xsurveyor = sp_cont (self, struct sp_xsurveyor, sockbase);
    data = sp_pipe_getdata (pipe);

    result = sp_list_empty (&xsurveyor->outpipes) ? 1 : 0;
    sp_list_insert (&xsurveyor->outpipes, &data->outitem,
        sp_list_end (&xsurveyor->outpipes));
    return result;
}

int sp_xsurveyor_send (struct sp_sockbase *self, const void *buf,
    size_t len)
{
    int rc;
    struct sp_list_item *it;
    struct sp_xsurveyor_data *data;
    struct sp_xsurveyor *xsurveyor;

    xsurveyor = sp_cont (self, struct sp_xsurveyor, sockbase);

    /*  Send the survey to all the respondents. */
    it = sp_list_begin (&xsurveyor->outpipes);
    while (it != sp_list_end (&xsurveyor->outpipes)) {
       data = sp_cont (it, struct sp_xsurveyor_data, outitem);
       rc = sp_pipe_send (data->pipe, buf, len);
       errnum_assert (rc >= 0, -rc);
       if (rc & SP_PIPE_RELEASE) {
           it = sp_list_erase (&xsurveyor->outpipes, it);
           continue;
       }
       it = sp_list_next (&xsurveyor->outpipes, it);
    }

    return 0;
}

int sp_xsurveyor_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_xsurveyor *xsurveyor;
    struct sp_list_item *it;

    xsurveyor = sp_cont (self, struct sp_xsurveyor, sockbase);

    /*  Current is NULL only when there are no avialable inbound pipes. */
    if (sp_slow (!xsurveyor->current))
        return -EAGAIN;

    /*  Get the messsage. */
    rc = sp_pipe_recv (xsurveyor->current->pipe, buf, len);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & SP_PIPE_RELEASE)
        it = sp_list_erase (&xsurveyor->inpipes, &xsurveyor->current->initem);
    else
        it = sp_list_next (&xsurveyor->inpipes, &xsurveyor->current->initem);
    if (!it)
        it = sp_list_begin (&xsurveyor->inpipes);
    xsurveyor->current = sp_cont (it, struct sp_xsurveyor_data, initem);

    return 0;
}

int sp_xsurveyor_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int sp_xsurveyor_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_xsurveyor_create (int fd)
{
    struct sp_xsurveyor *self;

    self = sp_alloc (sizeof (struct sp_xsurveyor), "socket (xsurveyor)");
    alloc_assert (self);
    sp_xsurveyor_init (self, &sp_xsurveyor_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xsurveyor_socktype_struct = {
    AF_SP_RAW,
    SP_SURVEYOR,
    sp_xsurveyor_create
};

struct sp_socktype *sp_xsurveyor_socktype = &sp_xsurveyor_socktype_struct;

