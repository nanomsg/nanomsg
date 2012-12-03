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

#include "sink.h"
#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

struct sp_sink_data {
    struct sp_pipe *pipe;
    struct sp_list_item item;
};

struct sp_sink {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes that we can get messages from. */
    struct sp_list pipes;

    /*  Next pipe to receive from. */
    struct sp_sink_data *current;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_sink_term (struct sp_sockbase *self);
static int sp_sink_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_sink_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sink_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sink_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sink_send (struct sp_sockbase *self, const void *buf, size_t len);
static int sp_sink_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_sink_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_sink_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_sink_sockbase_vfptr = {
    sp_sink_term,
    sp_sink_add,
    sp_sink_rm,
    sp_sink_in,
    sp_sink_out,
    sp_sink_send,
    sp_sink_recv,
    sp_sink_setopt,
    sp_sink_getopt
};

void sp_sink_init (struct sp_sink *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->pipes);
    self->current = NULL;
}

void sp_sink_term (struct sp_sockbase *self)
{
    struct sp_sink *sink;

    sink = sp_cont (self, struct sp_sink, sockbase);

    sp_list_term (&sink->pipes);
}

static int sp_sink_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_sink_data *data;

    data = sp_alloc (sizeof (struct sp_sink_data));
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

static void sp_sink_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_sink_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in inbound pipe list, remove it. */

    sp_free (data);
}

static int sp_sink_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_sink *sink;
    struct sp_sink_data *data;
    int result;

    sink = sp_cont (self, struct sp_sink, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&sink->pipes) ? 1 : 0;
    if (result)
        sink->current = data;
    sp_list_insert (&sink->pipes, &data->item, sp_list_end (&sink->pipes));
    return result;
}

static int sp_sink_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    /*  We are not going to send any messages, so there's no need to store
        the list of outbound pipes. */
    return 0;
}

static int sp_sink_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    /*  Sink can't send messages. */
    return -EAGAIN;
}

static int sp_sink_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_sink *sink;
    struct sp_list_item *it;

    sink = sp_cont (self, struct sp_sink, sockbase);

    /*  Current is NULL only when there are no avialable inbound pipes. */
    if (sp_slow (!sink->current))
        return -EAGAIN;

    /*  Get the messsage. */
    rc = sp_pipe_recv (sink->current->pipe, buf, len);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & SP_PIPE_RELEASE)
        it = sp_list_erase (&sink->pipes, &sink->current->item);
    else
        it = sp_list_next (&sink->pipes, &sink->current->item);
    if (!it)
        it = sp_list_begin (&sink->pipes);
    if (!it)
        sink->current = NULL;
    else
        sink->current = sp_cont (it, struct sp_sink_data, item);

    return 0;
}

static int sp_sink_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_sink_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_sink_create (int fd)
{
    struct sp_sink *self;

    self = sp_alloc (sizeof (struct sp_sink));
    alloc_assert (self);
    sp_sink_init (self, &sp_sink_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_sink_socktype_struct = {
    AF_SP,
    SP_SINK,
    sp_sink_create
};

struct sp_socktype *sp_sink_socktype = &sp_sink_socktype_struct;

