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

#include "xpush.h"

#include "../../sp.h"
#include "../../fanout.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

struct sp_xpush_data {
    struct sp_pipe *pipe;
    struct sp_list_item item;
};

struct sp_xpush {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes that we can get messages from. */
    struct sp_list pipes;

    /*  Next pipe to receive from. */
    struct sp_xpush_data *current;
};

/*  Private functions. */
static void sp_xpush_init (struct sp_xpush *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_xpush_term (struct sp_xpush *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_xpush_destroy (struct sp_sockbase *self);
static int sp_xpush_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_xpush_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpush_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpush_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_xpush_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_xpush_recv (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_xpush_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_xpush_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_xpush_sockbase_vfptr = {
    sp_xpush_destroy,
    sp_xpush_add,
    sp_xpush_rm,
    sp_xpush_in,
    sp_xpush_out,
    sp_xpush_send,
    sp_xpush_recv,
    sp_xpush_setopt,
    sp_xpush_getopt
};

static void sp_xpush_init (struct sp_xpush *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->pipes);
    self->current = NULL;
}

static void sp_xpush_term (struct sp_xpush *self)
{
    sp_list_term (&self->pipes);
}

void sp_xpush_destroy (struct sp_sockbase *self)
{
    struct sp_xpush *xpush;

    xpush = sp_cont (self, struct sp_xpush, sockbase);

    sp_xpush_term (xpush);
    sp_free (xpush);
}

static int sp_xpush_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xpush_data *data;

    data = sp_alloc (sizeof (struct sp_xpush_data), "pipe data (push)");
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

static void sp_xpush_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xpush_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in inbound pipe list, remove it. */

    sp_free (data);
}

static int sp_xpush_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    /*  We are not going to receive any messages, so there's no need to store
        the list of inbound pipes. */
    return 0;
}

static int sp_xpush_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xpush *xpush;
    struct sp_xpush_data *data;
    int result;

    xpush = sp_cont (self, struct sp_xpush, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&xpush->pipes) ? 1 : 0;
    if (result)
        xpush->current = data;
    sp_list_insert (&xpush->pipes, &data->item, sp_list_end (&xpush->pipes));
    return result;
}

static int sp_xpush_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_xpush *xpush;
    struct sp_list_item *it;

    xpush = sp_cont (self, struct sp_xpush, sockbase);

    /*  Current is NULL only when there are no avialable outbound pipes. */
    if (sp_slow (!xpush->current))
        return -EAGAIN;

    /*  Send the messsage. */
    rc = sp_pipe_send (xpush->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & SP_PIPE_RELEASE)
        it = sp_list_erase (&xpush->pipes, &xpush->current->item);
    else
        it = sp_list_next (&xpush->pipes, &xpush->current->item);
    if (!it)
        it = sp_list_begin (&xpush->pipes);
    xpush->current = sp_cont (it, struct sp_xpush_data, item);

    return 0;

}

static int sp_xpush_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    return -ENOTSUP;
}

static int sp_xpush_setopt (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_xpush_getopt (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

struct sp_sockbase *sp_xpush_create (int fd)
{
    struct sp_xpush *self;

    self = sp_alloc (sizeof (struct sp_xpush), "socket (push)");
    alloc_assert (self);
    sp_xpush_init (self, &sp_xpush_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xpush_socktype_struct = {
    AF_SP_RAW,
    SP_PUSH,
    sp_xpush_create
};

struct sp_socktype *sp_xpush_socktype = &sp_xpush_socktype_struct;

