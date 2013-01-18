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

#include "../../sp.h"
#include "../../fanin.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

struct sp_xsink_data {
    struct sp_pipe *pipe;
    struct sp_list_item item;
};

struct sp_xsink {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes that we can get messages from. */
    struct sp_list pipes;

    /*  Next pipe to receive from. */
    struct sp_xsink_data *current;
};

/*  Private functions. */
static void sp_xsink_init (struct sp_xsink *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_xsink_term (struct sp_xsink *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_xsink_destroy (struct sp_sockbase *self);
static int sp_xsink_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_xsink_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xsink_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xsink_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xsink_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_xsink_recv (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_xsink_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_xsink_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_xsink_sockbase_vfptr = {
    sp_xsink_destroy,
    sp_xsink_add,
    sp_xsink_rm,
    sp_xsink_in,
    sp_xsink_out,
    sp_xsink_send,
    sp_xsink_recv,
    sp_xsink_setopt,
    sp_xsink_getopt
};

static void sp_xsink_init (struct sp_xsink *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->pipes);
    self->current = NULL;
}

static void sp_xsink_term (struct sp_xsink *self)
{
    sp_list_term (&self->pipes);
}

void sp_xsink_destroy (struct sp_sockbase *self)
{
    struct sp_xsink *xsink;

    xsink = sp_cont (self, struct sp_xsink, sockbase);

    sp_xsink_term (xsink);
    sp_free (xsink);
}

static int sp_xsink_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsink_data *data;

    data = sp_alloc (sizeof (struct sp_xsink_data), "pipe data (sink)");
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

static void sp_xsink_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsink_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in inbound pipe list, remove it. */

    sp_free (data);
}

static int sp_xsink_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xsink *xsink;
    struct sp_xsink_data *data;
    int result;

    xsink = sp_cont (self, struct sp_xsink, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&xsink->pipes) ? 1 : 0;
    if (result)
        xsink->current = data;
    sp_list_insert (&xsink->pipes, &data->item, sp_list_end (&xsink->pipes));
    return result;
}

static int sp_xsink_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    /*  We are not going to send any messages, so there's no need to store
        the list of outbound pipes. */
    return 0;
}

static int sp_xsink_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    return -ENOTSUP;
}

static int sp_xsink_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_xsink *xsink;
    struct sp_list_item *it;

    xsink = sp_cont (self, struct sp_xsink, sockbase);

    /*  Current is NULL only when there are no avialable inbound pipes. */
    if (sp_slow (!xsink->current))
        return -EAGAIN;

    /*  Get the messsage. */
    rc = sp_pipe_recv (xsink->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & SP_PIPE_RELEASE)
        it = sp_list_erase (&xsink->pipes, &xsink->current->item);
    else
        it = sp_list_next (&xsink->pipes, &xsink->current->item);
    if (!it)
        it = sp_list_begin (&xsink->pipes);
    xsink->current = sp_cont (it, struct sp_xsink_data, item);

    return 0;
}

static int sp_xsink_setopt (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_xsink_getopt (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

struct sp_sockbase *sp_xsink_create (int fd)
{
    struct sp_xsink *self;

    self = sp_alloc (sizeof (struct sp_xsink), "socket (sink)");
    alloc_assert (self);
    sp_xsink_init (self, &sp_xsink_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xsink_socktype_struct = {
    AF_SP_RAW,
    SP_SINK,
    sp_xsink_create
};

struct sp_socktype *sp_xsink_socktype = &sp_xsink_socktype_struct;

