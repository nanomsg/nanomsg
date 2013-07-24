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

#ifndef NN_INS_INCLUDED
#define NN_INS_INCLUDED

#include "../../transport.h"

#include "../../utils/list.h"

struct nn_ins_item {

    /*  Every ins_item is an endpoint. */
    struct nn_epbase epbase;

    /*  Every ins_item is either in the list of bound or connected endpoints. */
    struct nn_list_item item;

    /*  This is the local cache of the endpoint's protocol ID. This way we can
        check the value without actually locking the object. */
    int protocol;

    /*  Number of connects on the fly. The object cannot be deallocated while
        this value is non-zero. */
    int connects;
};

void nn_ins_item_init (struct nn_ins_item *self,
    const struct nn_epbase_vfptr *vfptr, void *hint);
void nn_ins_item_term (struct nn_ins_item *self);

#endif

