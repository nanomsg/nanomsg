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

#include "priolist.h"
#include "cont.h"
#include "err.h"

#include <stddef.h>

void nn_priolist_init (struct nn_priolist *self)
{
    nn_list_init (&self->pipes);
    self->current = NULL;
}

void nn_priolist_term (struct nn_priolist *self)
{
    nn_list_term (&self->pipes);
}

void nn_priolist_add (struct nn_priolist *self, struct nn_pipe *pipe,
    struct nn_priolist_data *data)
{
    data->pipe = pipe;
}

void nn_priolist_rm (struct nn_priolist *self, struct nn_pipe *pipe,
    struct nn_priolist_data *data)
{
    /*  TODO: Shouldn't we actually remove the pipe from the list?
        Or do we assume that the pipe is removed by de-activating it? */
}

int nn_priolist_activate (struct nn_priolist *self, struct nn_pipe *pipe,
    struct nn_priolist_data *data)
{
    int empty;

    empty = nn_list_empty (&self->pipes) ? 1 : 0;
    if (empty)
        self->current = data;
    nn_list_insert (&self->pipes, &data->item, nn_list_end (&self->pipes));
    return empty;
}

struct nn_pipe *nn_priolist_getpipe (struct nn_priolist *self)
{
    return self->current ? self->current->pipe : NULL;
}

void nn_priolist_advance (struct nn_priolist *self, int release)
{
    struct nn_list_item *it;

    /*  Move the current pointer to next pipe. */
    if (release)
        it = nn_list_erase (&self->pipes, &self->current->item);
    else
        it = nn_list_next (&self->pipes, &self->current->item);
    if (!it)
        it = nn_list_begin (&self->pipes);
    self->current = nn_cont (it, struct nn_priolist_data, item);
}

