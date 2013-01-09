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
        are no key clashes even if the executable is re-started. Keys are
        31 bit unsigned integers. */
    sp_random_generate (&self->next_key, sizeof (self->next_key));
    self->next_key &= 0x7fffffff;

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

    sp_hash_insert (&xrep->pipes, xrep->next_key, &data->pipes);
    ++xrep->next_key;
    xrep->next_key &= 0x7fffffff;

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

int sp_xrep_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    int rc;
    uint32_t key;
    struct sp_xrep *xrep;
    struct sp_xrep_data *data;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    /*  We treat invalid peer ID as if the peer was non-existent. */
    if (len < sizeof (uint32_t))
        return 0;

    /*  Get the destination peer ID. Remove it from the message. */
    key = sp_getl (buf);
    buf = ((uint32_t*) buf) + 1;
    len -= sizeof (uint32_t);

    /*  Find the appropriate pipe to send the message to. If there's none,
        or if it's not ready for sending, silently drop the message. */
    data = sp_cont (sp_hash_get (&xrep->pipes, key), struct sp_xrep_data,
        pipes);
    if (!data || !(data->flags & SP_XREP_OUT))
        return 0;

    /*  Send the message. */
    rc = sp_pipe_send (data->pipe, buf, len);
    errnum_assert (rc >= 0, -rc);
    if (rc & SP_PIPE_RELEASE)
        data->flags &= ~SP_XREP_OUT;

    return 0;
}

int sp_xrep_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_xrep *xrep;
    struct sp_list_item *next;

    xrep = sp_cont (self, struct sp_xrep, sockbase);

    /*  If there are no inpipes, we can't recv a message. */
    if (sp_slow (!xrep->current))
        return -EAGAIN;

    /*  Get a message, tag it with the peer ID. */
    /*  TODO: Handle the pathological case below. */
    sp_assert (*len >= sizeof (uint32_t));
    *len -= sizeof (uint32_t);
    rc = sp_pipe_recv (xrep->current->pipe, ((uint32_t*) buf) + 1, len);
    errnum_assert (rc >= 0, -rc);
    sp_putl (buf, xrep->current->pipes.key);
    *len += sizeof (uint32_t);

    /*  Move the 'current' pointer to the next pipe. */
    if (rc & SP_PIPE_RELEASE)
        next = sp_list_erase (&xrep->inpipes, &xrep->current->inpipes);
    else
        next = sp_list_next (&xrep->inpipes, &xrep->current->inpipes);
    if (next == sp_list_end (&xrep->inpipes))
        next = sp_list_begin (&xrep->inpipes);
    xrep->current = sp_cont (next, struct sp_xrep_data, inpipes);

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

