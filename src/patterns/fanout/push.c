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

#include "push.h"
#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

struct sp_push_data {
    struct sp_pipe *pipe;
    struct sp_list_item item;
};

struct sp_push {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes that we can get messages from. */
    struct sp_list pipes;

    /*  Next pipe to receive from. */
    struct sp_push_data *current;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_push_term (struct sp_sockbase *self);
static int sp_push_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_push_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_push_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_push_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_push_send (struct sp_sockbase *self, const void *buf, size_t len);
static int sp_push_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_push_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_push_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_push_sockbase_vfptr = {
    sp_push_term,
    sp_push_add,
    sp_push_rm,
    sp_push_in,
    sp_push_out,
    sp_push_send,
    sp_push_recv,
    sp_push_setopt,
    sp_push_getopt
};

void sp_push_init (struct sp_push *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->pipes);
    self->current = NULL;
}

void sp_push_term (struct sp_sockbase *self)
{
    struct sp_push *push;

    push = sp_cont (self, struct sp_push, sockbase);

    sp_list_term (&push->pipes);
}

static int sp_push_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_push_data *data;

    data = sp_alloc (sizeof (struct sp_push_data));
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

static void sp_push_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_push_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in inbound pipe list, remove it. */

    sp_free (data);
}

static int sp_push_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    /*  We are not going to receive any messages, so there's no need to store
        the list of inbound pipes. */
    return 0;
}

static int sp_push_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_push *push;
    struct sp_push_data *data;
    int result;

    push = sp_cont (self, struct sp_push, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&push->pipes) ? 1 : 0;
    if (result)
        push->current = data;
    sp_list_insert (&push->pipes, &data->item, sp_list_end (&push->pipes));
    return result;
}

static int sp_push_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    int rc;
    struct sp_push *push;

    push = sp_cont (self, struct sp_push, sockbase);

    /*  Current is NULL only when there are no avialable outbound pipes. */
    if (sp_slow (!push->current))
        return -EAGAIN;

    /*  Send the messsage. */
    rc = sp_pipe_send (push->current->pipe, buf, len);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & SP_PIPE_RELEASE)
        push->current = sp_cont (sp_list_erase (&push->pipes,
            &push->current->item), struct sp_push_data, item);
    else
        push->current = sp_cont (sp_list_next (&push->pipes,
            &push->current->item), struct sp_push_data, item);
    if (!push->current)
        push->current = sp_cont (sp_list_begin (&push->pipes),
            struct sp_push_data, item);

    return 0;

}

static int sp_push_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    /*  Push socket can't receive messages. */
    return -EAGAIN;
}

static int sp_push_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_push_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_push_create (int fd)
{
    struct sp_push *self;

    self = sp_alloc (sizeof (struct sp_push));
    alloc_assert (self);
    sp_push_init (self, &sp_push_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_push_socktype_struct = {
    AF_SP,
    SP_PUSH,
    sp_push_create
};

struct sp_socktype *sp_push_socktype = &sp_push_socktype_struct;

