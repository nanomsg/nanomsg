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

#ifndef NN_INPROCB_INCLUDED
#define NN_INPROCB_INCLUDED

#include "../../transport.h"

#include "../../utils/list.h"

#include "msgpipe.h"

#define NN_INPROCB_FLAG_TERMINATING 1

struct nn_inprocb {

    /*  This object is an endpoint. */
    struct nn_epbase epbase;

    /*  This object is an element in the list of all bound endpoints managed
        by nn_inproc_ctx object. */
    struct nn_list_item list;

    /*  List of all message pipes attached to this endpoint. */
    struct nn_list pipes;

    /*  Any combination of the flags defined above. */
    int flags;
};

int nn_inprocb_init (struct nn_inprocb *self, const char *addr, void *hint);
const char *nn_inprocb_getaddr (struct nn_inprocb *self);
int nn_inprocb_socktype (struct nn_inprocb *self);
int nn_inprocb_ispeer (struct nn_inprocb *self, int socktype);
void nn_inprocb_add_pipe (struct nn_inprocb *self, struct nn_msgpipe *pipe);
void nn_inprocb_rm_pipe (struct nn_inprocb *self, struct nn_msgpipe *pipe);

#endif
