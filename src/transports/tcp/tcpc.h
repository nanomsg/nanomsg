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

#ifndef SP_TCPC_INCLUDED
#define SP_TCPC_INCLUDED

#include "../../transport.h"

#include "../../utils/aio.h"
#include "../../utils/stream.h"

struct sp_tcpc {

    /*  Event sink. */
    const struct sp_cp_sink *sink;

    /*  This object is an endpoint. */
    struct sp_epbase epbase;

    /*  The underlying TCP socket. */
    struct sp_usock usock;

    /*  There's at most one session per connecting endpoint, thus we can
        embed the session object directly into the connecter class. */
    struct sp_stream stream;

    /*  Timer to wait before retrying to connect. */
    struct sp_timer retry_timer;
};

int sp_tcpc_init (struct sp_tcpc *self, const char *addr, void *hint);

#endif

