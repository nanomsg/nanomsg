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

#include "../../sp.h"
#include "../../reqrep.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"

#include <string.h>

/*  Private functions. */
static void sp_xrep_destroy (struct sp_sockbase *self);

static const struct sp_sockbase_vfptr sp_xrep_sockbase_vfptr = {
    sp_xrep_destroy,
    sp_xrep_add,
    sp_xrep_rm,
    sp_xrep_in,
    sp_xrep_out,
    sp_xrep_send,
    sp_xrep_recv,
    sp_xrep_setopt,
    sp_xrep_getopt
};

void sp_xrep_init (struct sp_xrep *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);

    /*  Start assigning keys beginning with a random number. This way there
        are no key clashes even if the executable is re-started. */
    sp_random_generate (&self->next_key, sizeof (self->next_key));

    sp_hash_init (&self->pipes);
    sp_list_init (&self->inpipes);
    self->current = NULL;
}

void sp_xrep_term (struct sp_xrep *self)
{
    sp_list_term (&self->inpipes);
    sp_hash_term (&self->pipes);
}

static void sp_xrep_destroy (struct sp_sockbase *self)
{
    struct sp_xrep *xrep;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    sp_xrep_term (xrep);
    sp_free (xrep);
}

int sp_xrep_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xrep *xrep;
    struct sp_xrep_data *data;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    data = sp_alloc (sizeof (struct sp_xrep_data), "pipe data (xrep)");
    alloc_assert (data);
    data->pipe = pipe;
    data->flags = 0;

    sp_pipe_setdata (pipe, data);

    sp_hash_insert (&xrep->pipes, xrep->next_key & 0x7fffffff, &data->pipes);
    ++xrep->next_key;

    return 0;
}

void sp_xrep_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xrep *xrep;
    struct sp_xrep_data *data;

    xrep = sp_cont (self, struct sp_xrep, sockbase);
    data = sp_pipe_getdata (pipe);

    /*  TODO: If pipe is in the pipe lists, remove it. Move the 'current'
        pointer as well. */

    sp_hash_erase (&xrep->pipes, &data->pipes);

    sp_free (data);
}

int sp_xrep_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xrep *xrep;
    struct sp_xrep_data *data;
    int result;

    xrep = sp_cont (self, struct sp_xrep, sockbase);
    data = sp_pipe_getdata (pipe);
    result = sp_list_empty (&xrep->inpipes) ? 1 : 0;
    sp_list_insert (&xrep->inpipes, &data->inpipes,
        sp_list_end (&xrep->inpipes));
    if (!xrep->current)
        xrep->current = data;

    return result;
}

int sp_xrep_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    struct sp_xrep_data *data;

    data = sp_pipe_getdata (pipe);
    data->flags |= SP_XREP_OUT;

    /*  XREP socket never blocks on send, so there's no point in unblocking. */
    return 0;
}

int sp_xrep_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    uint32_t key;
    struct sp_xrep *xrep;
    struct sp_xrep_data *data;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    /*  We treat invalid peer ID as if the peer was non-existent. */
    if (sp_slow (sp_chunkref_size (&msg->hdr) < sizeof (uint32_t))) {
        sp_msg_term (msg);
        return 0;
    }

    /*  Retrieve the destination peer ID. Trim it from the header. */
    key = sp_getl (sp_chunkref_data (&msg->hdr));
    sp_chunkref_trim (&msg->hdr, 4);

    /*  Find the appropriate pipe to send the message to. If there's none,
        or if it's not ready for sending, silently drop the message. */
    data = sp_cont (sp_hash_get (&xrep->pipes, key), struct sp_xrep_data,
        pipes);
    if (!data || !(data->flags & SP_XREP_OUT))
        return 0;

    /*  Send the message. */
    rc = sp_pipe_send (data->pipe, msg);
    errnum_assert (rc >= 0, -rc);
    if (rc & SP_PIPE_RELEASE)
        data->flags &= ~SP_XREP_OUT;

    return 0;
}

int sp_xrep_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_xrep *xrep;
    int i;
    void *data;
    size_t sz;
    struct sp_list_item *next;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    /*  If there are no inpipes, we can't recv a message. */
    if (sp_slow (!xrep->current))
        return -EAGAIN;

    /*  Get a request. */
    rc = sp_pipe_recv (xrep->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    /*  Move the 'current' pointer to the next pipe. */
    if (rc & SP_PIPE_RELEASE)
        next = sp_list_erase (&xrep->inpipes, &xrep->current->inpipes);
    else
        next = sp_list_next (&xrep->inpipes, &xrep->current->inpipes);
    if (next == sp_list_end (&xrep->inpipes))
        next = sp_list_begin (&xrep->inpipes);
    xrep->current = sp_cont (next, struct sp_xrep_data, inpipes);

    /*  Determine the size of the message header. */
    i = 0;
    data = sp_chunkref_data (&msg->body);
    sz = sp_chunkref_size (&msg->body);
    while (1) {

        /*  Ignore the malformed requests without the bottom of the stack. */
        if (sp_slow (i * sizeof (uint32_t) > sz)) {
            sp_msg_term (msg);
            return -EAGAIN;
        }

        /*  If the bottom of the backtrace stack is reached, proceed. */
        if (sp_getl ((uint8_t*)(((uint32_t*) data) + i)) & 0x80000000)
            break;

        ++i;
    }
    ++i;

    /*  Split the header and the body. Append new element to the backtrace. */
    sp_assert (sp_chunkref_size (&msg->hdr) == 0);
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_init (&msg->hdr, (i + 1) * sizeof (uint32_t));
    sp_assert (!(xrep->current->pipes.key & 0x80000000));
    sp_putl (sp_chunkref_data (&msg->hdr), xrep->current->pipes.key);
    memcpy (((uint32_t*) sp_chunkref_data (&msg->hdr)) + 1, data,
        i * sizeof (uint32_t));
    sp_chunkref_trim (&msg->body, i * sizeof (uint32_t));

    return 0;
}

int sp_xrep_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int sp_xrep_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_xrep_create (int fd)
{
    struct sp_xrep *self;

    self = sp_alloc (sizeof (struct sp_xrep), "socket (xrep)");
    alloc_assert (self);
    sp_xrep_init (self, &sp_xrep_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xrep_socktype_struct = {
    AF_SP_RAW,
    SP_REP,
    sp_xrep_create
};

struct sp_socktype *sp_xrep_socktype = &sp_xrep_socktype_struct;

