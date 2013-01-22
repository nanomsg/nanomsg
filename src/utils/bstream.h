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

#ifndef NN_BSTREAM_INCLUDED
#define NN_BSTREAM_INCLUDED

#include "../transport.h"

#include "aio.h"

struct nn_astream;

/*  Bound stream socket. */

struct nn_bstream {

    /*  Event sink. */
    const struct nn_cp_sink *sink;

    /*  This object is an endpoint. */
    struct nn_epbase epbase;

    /*  The listening socket. */
    struct nn_usock usock;

    /*  List of all sockets accepted via this endpoint. */
    struct nn_list astreams;
};

int nn_bstream_init (struct nn_bstream *self, const char *addr, void *hint,
    int (*initfn) (const char *addr, struct nn_usock *usock, struct nn_cp *cp,
    int backlog), int backlog);

void nn_bstream_astream_closed (struct nn_bstream *self,
    struct nn_astream *astream);

#endif
