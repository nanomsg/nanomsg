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

#include "tcp.h"
#include "btcp.h"
#include "ctcp.h"

#include "../../tcp.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"

#include <string.h>
#include <sys/un.h>
#include <unistd.h>

/*  nn_transport interface. */
static int nn_tcp_bind (void *hint, struct nn_epbase **epbase);
static int nn_tcp_connect (void *hint, struct nn_epbase **epbase);

static struct nn_transport nn_tcp_vfptr = {
    "tcp",
    NN_TCP,
    NULL,
    NULL,
    nn_tcp_bind,
    nn_tcp_connect,
    NULL,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_tcp = &nn_tcp_vfptr;

static int nn_tcp_bind (void *hint, struct nn_epbase **epbase)
{
    /*  TODO: Check the syntax of the address here! */
    return nn_btcp_create (hint, epbase);
}

static int nn_tcp_connect (void *hint, struct nn_epbase **epbase)
{
    /*  TODO: Check the syntax of the address here! */
    return nn_ctcp_create (hint, epbase);
}

