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

#include "xsurveyor.h"

#include "../../nn.h"
#include "../../survey.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

#include <stddef.h>

/*  Private functions. */
static void nn_xsurveyor_destroy (struct nn_sockbase *self);

/*  Implementation of nn_sockbase's virtual functions. */
static const struct nn_sockbase_vfptr nn_xsurveyor_sockbase_vfptr = {
    nn_xsurveyor_destroy,
    nn_xsurveyor_add,
    nn_xsurveyor_rm,
    nn_xsurveyor_in,
    nn_xsurveyor_out,
    nn_xsurveyor_send,
    nn_xsurveyor_recv,
    nn_xsurveyor_setopt,
    nn_xsurveyor_getopt
};

void nn_xsurveyor_init (struct nn_xsurveyor *self,
    const struct nn_sockbase_vfptr *vfptr, int fd)
{
    nn_sockbase_init (&self->sockbase, vfptr, fd);
    nn_list_init (&self->outpipes);
    nn_list_init (&self->inpipes);
    self->current = NULL;
}

void nn_xsurveyor_term (struct nn_xsurveyor *self)
{
    nn_list_term (&self->inpipes);
    nn_list_term (&self->outpipes);
}

static void nn_xsurveyor_destroy (struct nn_sockbase *self)
{
    struct nn_xsurveyor *xsurveyor;

    xsurveyor = nn_cont (self, struct nn_xsurveyor, sockbase);

    nn_xsurveyor_term (xsurveyor);
    nn_free (xsurveyor);
}

int nn_xsurveyor_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsurveyor_data *data;

    data = nn_alloc (sizeof (struct nn_xsurveyor_data),
        "pipe data (xsurveyor)");
    alloc_assert (data);
    data->pipe = pipe;
    nn_pipe_setdata (pipe, data);

    return 0;
}

void nn_xsurveyor_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsurveyor_data *data;

    data = nn_pipe_getdata (pipe);

    /*  TODO: If pipe is in the pipe lists, remove it. */

    nn_free (data);
}

int nn_xsurveyor_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsurveyor *xsurveyor;
    struct nn_xsurveyor_data *data;
    int result;

    xsurveyor = nn_cont (self, struct nn_xsurveyor, sockbase);
    data = nn_pipe_getdata (pipe);
    result = nn_list_empty (&xsurveyor->inpipes) ? 1 : 0;
    if (result)
        xsurveyor->current = data;
    nn_list_insert (&xsurveyor->inpipes, &data->initem,
        nn_list_end (&xsurveyor->inpipes));
    return result;
}

int nn_xsurveyor_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xsurveyor *xsurveyor;
    struct nn_xsurveyor_data *data;
    int result;

    xsurveyor = nn_cont (self, struct nn_xsurveyor, sockbase);
    data = nn_pipe_getdata (pipe);

    result = nn_list_empty (&xsurveyor->outpipes) ? 1 : 0;
    nn_list_insert (&xsurveyor->outpipes, &data->outitem,
        nn_list_end (&xsurveyor->outpipes));
    return result;
}

int nn_xsurveyor_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_list_item *it;
    struct nn_xsurveyor_data *data;
    struct nn_xsurveyor *xsurveyor;
    struct nn_msg copy;

    xsurveyor = nn_cont (self, struct nn_xsurveyor, sockbase);

    /*  Send the survey to all the respondents. */
    it = nn_list_begin (&xsurveyor->outpipes);
    while (it != nn_list_end (&xsurveyor->outpipes)) {
       data = nn_cont (it, struct nn_xsurveyor_data, outitem);
       nn_msg_cp (&copy, msg);
       rc = nn_pipe_send (data->pipe, &copy);
       errnum_assert (rc >= 0, -rc);
       if (rc & NN_PIPE_RELEASE) {
           it = nn_list_erase (&xsurveyor->outpipes, it);
           continue;
       }
       it = nn_list_next (&xsurveyor->outpipes, it);
    }

    /*  Drop the reference to the message. */
    nn_msg_term (msg);

    return 0;
}

int nn_xsurveyor_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xsurveyor *xsurveyor;
    struct nn_list_item *it;

    xsurveyor = nn_cont (self, struct nn_xsurveyor, sockbase);

    /*  Current is NULL only when there are no avialable inbound pipes. */
    if (nn_slow (!xsurveyor->current))
        return -EAGAIN;

    /*  Get the messsage. */
    rc = nn_pipe_recv (xsurveyor->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & NN_PIPE_RELEASE)
        it = nn_list_erase (&xsurveyor->inpipes, &xsurveyor->current->initem);
    else
        it = nn_list_next (&xsurveyor->inpipes, &xsurveyor->current->initem);
    if (!it)
        it = nn_list_begin (&xsurveyor->inpipes);
    xsurveyor->current = nn_cont (it, struct nn_xsurveyor_data, initem);

    /*  Split the header from the body, if needed. */
    if (!(rc & NN_PIPE_PARSED)) {
        if (nn_slow (nn_chunkref_size (&msg->body) < sizeof (uint32_t))) {
            nn_msg_term (msg);
            return -EAGAIN;
        }
        nn_assert (nn_chunkref_size (&msg->hdr) == 0);
        nn_chunkref_term (&msg->hdr);
        nn_chunkref_init (&msg->hdr, sizeof (uint32_t));
        memcpy (nn_chunkref_data (&msg->hdr), nn_chunkref_data (&msg->body),
           sizeof (uint32_t));
        nn_chunkref_trim (&msg->body, sizeof (uint32_t));
    }

    return 0;
}

int nn_xsurveyor_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xsurveyor_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xsurveyor_sethdr (struct nn_msg *msg, const void *hdr, size_t hdrlen)
{
    /*  TODO */
    nn_assert (0);
}

int nn_xsurveyor_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    /*  TODO */
    nn_assert (0);
}

static struct nn_sockbase *nn_xsurveyor_create (int fd)
{
    struct nn_xsurveyor *self;

    self = nn_alloc (sizeof (struct nn_xsurveyor), "socket (xsurveyor)");
    alloc_assert (self);
    nn_xsurveyor_init (self, &nn_xsurveyor_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct nn_socktype nn_xsurveyor_socktype_struct = {
    AF_SP_RAW,
    NN_SURVEYOR,
    nn_xsurveyor_create
};

struct nn_socktype *nn_xsurveyor_socktype = &nn_xsurveyor_socktype_struct;

