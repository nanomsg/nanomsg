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

#include "ins.h"

#include "../../utils/err.h"

void nn_ins_item_init (struct nn_ins_item *self,
    const struct nn_epbase_vfptr *vfptr, void *hint)
{
    int protocol;
    size_t sz;

    nn_epbase_init (&self->epbase, vfptr, hint);
    nn_list_item_init (&self->item);
    sz = sizeof (self->protocol);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
        &self->protocol, &sz);
    nn_assert (sz == sizeof (self->protocol));
    self->connects = 0;
}

void nn_ins_item_term (struct nn_ins_item *self)
{
    nn_list_item_term (&self->item);
    nn_epbase_term (&self->epbase);
}

