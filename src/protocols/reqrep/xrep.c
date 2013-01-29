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

#include "xrep.h"

#include "../../nn.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"

#include <string.h>

/*  Private functions. */
static void nn_xrep_destroy (struct nn_sockbase *self);

static const struct nn_sockbase_vfptr nn_xrep_sockbase_vfptr = {
    nn_xrep_destroy,
    nn_xrep_add,
    nn_xrep_rm,
    nn_xrep_in,
    nn_xrep_out,
    nn_xrep_send,
    nn_xrep_recv,
    nn_xrep_setopt,
    nn_xrep_getopt,
    nn_xrep_sethdr,
    nn_xrep_gethdr
};

void nn_xrep_init (struct nn_xrep *self, const struct nn_sockbase_vfptr *vfptr,
    int fd)
{
    nn_sockbase_init (&self->sockbase, vfptr, fd);

    /*  Start assigning keys beginning with a random number. This way there
        are no key clashes even if the executable is re-started. */
    nn_random_generate (&self->next_key, sizeof (self->next_key));

    nn_hash_init (&self->pipes);
    nn_list_init (&self->inpipes);
    self->current = NULL;
}

void nn_xrep_term (struct nn_xrep *self)
{
    nn_list_term (&self->inpipes);
    nn_hash_term (&self->pipes);
}

static void nn_xrep_destroy (struct nn_sockbase *self)
{
    struct nn_xrep *xrep;

    xrep = nn_cont (self, struct nn_xrep, sockbase);

    nn_xrep_term (xrep);
    nn_free (xrep);
}

int nn_xrep_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xrep *xrep;
    struct nn_xrep_data *data;

    xrep = nn_cont (self, struct nn_xrep, sockbase);

    data = nn_alloc (sizeof (struct nn_xrep_data), "pipe data (xrep)");
    alloc_assert (data);
    data->pipe = pipe;
    data->flags = 0;

    nn_pipe_setdata (pipe, data);

    nn_hash_insert (&xrep->pipes, xrep->next_key & 0x7fffffff, &data->pipes);
    ++xrep->next_key;

    return 0;
}

void nn_xrep_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xrep *xrep;
    struct nn_xrep_data *data;

    xrep = nn_cont (self, struct nn_xrep, sockbase);
    data = nn_pipe_getdata (pipe);

    /*  TODO: If pipe is in the pipe lists, remove it. Move the 'current'
        pointer as well. */

    nn_hash_erase (&xrep->pipes, &data->pipes);

    nn_free (data);
}

int nn_xrep_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xrep *xrep;
    struct nn_xrep_data *data;
    int result;

    xrep = nn_cont (self, struct nn_xrep, sockbase);
    data = nn_pipe_getdata (pipe);
    result = nn_list_empty (&xrep->inpipes) ? 1 : 0;
    nn_list_insert (&xrep->inpipes, &data->inpipes,
        nn_list_end (&xrep->inpipes));
    if (!xrep->current)
        xrep->current = data;

    return result;
}

int nn_xrep_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    struct nn_xrep_data *data;

    data = nn_pipe_getdata (pipe);
    data->flags |= NN_XREP_OUT;

    /*  XREP socket never blocks on send, so there's no point in unblocking. */
    return 0;
}

int nn_xrep_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    uint32_t key;
    struct nn_xrep *xrep;
    struct nn_xrep_data *data;

    xrep = nn_cont (self, struct nn_xrep, sockbase);

    /*  We treat invalid peer ID as if the peer was non-existent. */
    if (nn_slow (nn_chunkref_size (&msg->hdr) < sizeof (uint32_t))) {
        nn_msg_term (msg);
        return 0;
    }

    /*  Retrieve the destination peer ID. Trim it from the header. */
    key = nn_getl (nn_chunkref_data (&msg->hdr));
    nn_chunkref_trim (&msg->hdr, 4);

    /*  Find the appropriate pipe to send the message to. If there's none,
        or if it's not ready for sending, silently drop the message. */
    data = nn_cont (nn_hash_get (&xrep->pipes, key), struct nn_xrep_data,
        pipes);
    if (!data || !(data->flags & NN_XREP_OUT))
        return 0;

    /*  Send the message. */
    rc = nn_pipe_send (data->pipe, msg);
    errnum_assert (rc >= 0, -rc);
    if (rc & NN_PIPE_RELEASE)
        data->flags &= ~NN_XREP_OUT;

    return 0;
}

int nn_xrep_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_xrep *xrep;
    int i;
    void *data;
    size_t sz;
    struct nn_list_item *next;
    struct nn_chunkref ref;

    xrep = nn_cont (self, struct nn_xrep, sockbase);

    /*  If there are no inpipes, we can't recv a message. */
    if (nn_slow (!xrep->current))
        return -EAGAIN;

    /*  Get a request. */
    rc = nn_pipe_recv (xrep->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    if (!(rc & NN_PIPE_PARSED)) {

        /*  Determine the size of the message header. */
        data = nn_chunkref_data (&msg->body);
        sz = nn_chunkref_size (&msg->body);
        i = 0;
        while (1) {

            /*  Ignore the malformed requests without the bottom of the stack. */
            if (nn_slow ((i + 1) * sizeof (uint32_t) > sz)) {
                nn_msg_term (msg);
                return -EAGAIN;
            }

            /*  If the bottom of the backtrace stack is reached, proceed. */
            if (nn_getl ((uint8_t*)(((uint32_t*) data) + i)) & 0x80000000)
                break;

            ++i;
        }
        ++i;

        /*  Split the header and the body. */
        nn_assert (nn_chunkref_size (&msg->hdr) == 0);
        nn_chunkref_term (&msg->hdr);
        nn_chunkref_init (&msg->hdr, i * sizeof (uint32_t));
        memcpy (nn_chunkref_data (&msg->hdr), data, i * sizeof (uint32_t));
        nn_chunkref_trim (&msg->body, i * sizeof (uint32_t));
    }

    /*  Prepend the header by the pipe key. */
    nn_chunkref_init (&ref, nn_chunkref_size (&msg->hdr) + sizeof (uint32_t));
    nn_putl (nn_chunkref_data (&ref), xrep->current->pipes.key);
    memcpy (((uint8_t*) nn_chunkref_data (&ref)) + sizeof (uint32_t),
        nn_chunkref_data (&msg->hdr), nn_chunkref_size (&msg->hdr));
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_mv (&msg->hdr, &ref);

    /*  Move the 'current' pointer to the next pipe. */
    if (rc & NN_PIPE_RELEASE)
        next = nn_list_erase (&xrep->inpipes, &xrep->current->inpipes);
    else
        next = nn_list_next (&xrep->inpipes, &xrep->current->inpipes);
    if (next == nn_list_end (&xrep->inpipes))
        next = nn_list_begin (&xrep->inpipes);
    xrep->current = nn_cont (next, struct nn_xrep_data, inpipes);

    return 0;
}

int nn_xrep_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xrep_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

int nn_xrep_sethdr (struct nn_msg *msg, const void *hdr, size_t hdrlen)
{
    /*  TODO */
    nn_assert (0);
}

int nn_xrep_gethdr (struct nn_msg *msg, void *hdr, size_t *hdrlen)
{
    /*  TODO */
    nn_assert (0);
}

static struct nn_sockbase *nn_xrep_create (int fd)
{
    struct nn_xrep *self;

    self = nn_alloc (sizeof (struct nn_xrep), "socket (xrep)");
    alloc_assert (self);
    nn_xrep_init (self, &nn_xrep_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct nn_socktype nn_xrep_socktype_struct = {
    AF_SP_RAW,
    NN_REP,
    nn_xrep_create
};

struct nn_socktype *nn_xrep_socktype = &nn_xrep_socktype_struct;

