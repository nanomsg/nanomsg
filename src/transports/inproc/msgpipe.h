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

#ifndef NN_MSGPIPE_INCLUDED
#define NN_MSGPIPE_INCLUDED

#include "../../transport.h"

#include "msgqueue.h"

#include "../../utils/aio.h"

struct nn_inprocb;
struct nn_inprocc;

/*  This object (but not the msgqueues inside it) is synchronised using
    the inproc_ctx critical section. */

#define NN_MSGPIPE_PIPE0_ACTIVE 1
#define NN_MSGPIPE_PIPE1_ACTIVE 2

#define NN_MSGPIPE_EVENT_IN 1
#define NN_MSGPIPE_EVENT_OUT 2

struct nn_msgpipe {

    /*  The two ends of the pipe. First one is the bound end, second one
        is the connected end. */
    struct nn_pipebase pipes [2];

    /*  TODO: These flags are good only for asserting there's no bug in the
        library. Later on they should be removed. */
    int flags;

    /*  The two underlying queues to achieve bi-directional communication. */
    struct nn_msgqueue queues [2];

    /*  Incoming notifications. */
    struct nn_event inevents [2];
    struct nn_event outevents [2];

    /*  The pipe is owned by exactly one bound endpoint. */
    struct nn_list_item inprocb;

};

void nn_msgpipe_init (struct nn_msgpipe *self,
    struct nn_inprocb *inprocb, struct nn_inprocc *inprocc);
void nn_msgpipe_detachb (struct nn_msgpipe *self);
void nn_msgpipe_detachc (struct nn_msgpipe *self);

#endif
