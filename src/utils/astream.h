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

#ifndef NN_ASTREAM_INCLUDED
#define NN_ASTREAM_INCLUDED

#include "aio.h"
#include "bstream.h"
#include "stream.h"
#include "list.h"

/*  Accepted stream socket. */

struct nn_astream {

    /*  Event sink. */
    const struct nn_cp_sink *sink;

    /*  The undelying TCP socket. */
    struct nn_usock usock;

    /*  TCP session state machine. */
    struct nn_stream stream;

    /*  Bound socket that created this connection. */
    struct nn_bstream *bstream;

    /*  The object is part of nn_tcpb's list of accepted sockets. */
    struct nn_list_item item;
};

void nn_astream_init (struct nn_astream *self, struct nn_epbase *epbase,
    int s, struct nn_usock *usock, struct nn_bstream *bstream);
void nn_astream_close (struct nn_astream *self);

#endif

