/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "fq.h"
#include "err.h"
#include "cont.h"

#include <stddef.h>

void nn_fq_init (struct nn_fq *self)
{
    nn_list_init (&self->pipes);
    self->current = NULL;
}

void nn_fq_term (struct nn_fq *self)
{
    nn_list_term (&self->pipes);
}

int nn_fq_add (struct nn_fq *self, struct nn_pipe *pipe,
    struct nn_fq_data *data)
{
    data->pipe = pipe;
    return 0;
}

void nn_fq_rm (struct nn_fq *self, struct nn_pipe *pipe,
    struct nn_fq_data *data)
{
    /*  TODO: Shouldn't we actually remove the pipe from the list? */
}

int nn_fq_in (struct nn_fq *self, struct nn_pipe *pipe,
    struct nn_fq_data *data)
{
    int empty;

    empty = nn_list_empty (&self->pipes) ? 1 : 0;
    if (empty)
        self->current = data;
    nn_list_insert (&self->pipes, &data->item, nn_list_end (&self->pipes));
    return empty;
}

int nn_fq_recv (struct nn_fq *self, struct nn_msg *msg)
{
    int rc;
    struct nn_list_item *it;

    /*  Current is NULL only when there are no avialable outbound pipes. */
    if (nn_slow (!self->current))
        return -EAGAIN;

    /*  Receive the messsage. */
    rc = nn_pipe_recv (self->current->pipe, msg);
    errnum_assert (rc >= 0, -rc);

    /*  Move the current pointer to next pipe. */
    if (rc & NN_PIPE_RELEASE)
        it = nn_list_erase (&self->pipes, &self->current->item);
    else
        it = nn_list_next (&self->pipes, &self->current->item);
    if (!it)
        it = nn_list_begin (&self->pipes);
    self->current = nn_cont (it, struct nn_fq_data, item);

    return 0;
}

