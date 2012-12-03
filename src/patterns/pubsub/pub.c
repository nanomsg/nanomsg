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

#include "pub.h"
#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/alloc.h"

struct sp_pub_data {
    struct sp_pipe *pipe;
    struct sp_list_item out;
};

struct sp_pub {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes ready to accept messages. */
    struct sp_list pipes;

};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_pub_term (struct sp_sockbase *self);
static int sp_pub_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_pub_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pub_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pub_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_pub_send (struct sp_sockbase *self, const void *buf, size_t len);
static int sp_pub_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_pub_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_pub_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_pub_sockbase_vfptr = {
    sp_pub_term,
    sp_pub_add,
    sp_pub_rm,
    sp_pub_in,
    sp_pub_out,
    sp_pub_send,
    sp_pub_recv,
    sp_pub_setopt,
    sp_pub_getopt
};

void sp_pub_init (struct sp_pub *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_list_init (&self->pipes);
}

void sp_pub_term (struct sp_sockbase *self)
{
    struct sp_pub *pub;

    pub = sp_cont (self, struct sp_pub, sockbase);

    sp_list_term (&pub->pipes);
}

static int sp_pub_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_pub_data *data;

    data = sp_alloc (sizeof (struct sp_pub_data));
    alloc_assert (data);
    data->pipe = pipe;
    sp_pipe_setdata (pipe, data);

    return 0;
}

static void sp_pub_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_pub_data *data;

    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in outbound pipe list, remove it. */

    sp_free (data);
}

static int sp_pub_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    /*  We shouldn't get any messages from subscribers. */
    sp_assert (0);
}

static int sp_pub_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_pub *pub;
    struct sp_pub_data *data;
    int result;

    pub = sp_cont (self, struct sp_pub, sockbase);
    data = sp_pipe_getdata (pipe);

    /*  New subscriber. Let's remember it. */
    result = sp_list_empty (&pub->pipes) ? 1 : 0;
    sp_list_insert (&pub->pipes, &data->out, sp_list_end (&pub->pipes));
    return result;
}

static int sp_pub_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    int rc;
    struct sp_list_item *it;
    struct sp_pub_data *data;
    struct sp_pub *pub;

    pub = sp_cont (self, struct sp_pub, sockbase);

    /*  Send the message to all the subscribers. */
    it = sp_list_begin (&pub->pipes);
    while (it != sp_list_end (&pub->pipes)) {
       data = sp_cont (it, struct sp_pub_data, out);
       rc = sp_pipe_send (data->pipe, buf, len);
       errnum_assert (rc >= 0, -rc);
       if (rc & SP_PIPE_RELEASE) {
           it = sp_list_erase (&pub->pipes, it);
           continue;
       }
       it = sp_list_next (&pub->pipes, it);
    }

    return 0;
}

static int sp_pub_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    /*  Cannot receive messages from PUB socket. */
    return -EAGAIN;
}

static int sp_pub_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

static int sp_pub_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_pub_create (int fd)
{
    struct sp_pub *self;

    self = sp_alloc (sizeof (struct sp_pub));
    alloc_assert (self);
    sp_pub_init (self, &sp_pub_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_pub_socktype_struct = {
    AF_SP,
    SP_PUB,
    sp_pub_create
};

struct sp_socktype *sp_pub_socktype = &sp_pub_socktype_struct;

