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

#ifndef SP_ASTREAM_INCLUDED
#define SP_ASTREAM_INCLUDED

#include "aio.h"
#include "bstream.h"
#include "stream.h"
#include "list.h"

/*  Accepted stream socket. */

struct sp_astream {

    /*  Event sink. */
    const struct sp_cp_sink *sink;

    /*  The undelying TCP socket. */
    struct sp_usock usock;

    /*  TCP session state machine. */
    struct sp_stream stream;

    /*  Bound socket that created this connection. */
    struct sp_bstream *bstream;

    /*  The object is part of sp_tcpb's list of accepted sockets. */
    struct sp_list_item item;
};

void sp_astream_init (struct sp_astream *self, struct sp_epbase *epbase,
    int s, struct sp_usock *usock, struct sp_bstream *bstream);
void sp_astream_close (struct sp_astream *self);

#endif

