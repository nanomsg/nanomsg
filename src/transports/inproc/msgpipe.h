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

#ifndef SP_MSGPIPE_INCLUDED
#define SP_MSGPIPE_INCLUDED

#include "../../transport.h"

#include "msgqueue.h"

#include "../../utils/aio.h"

struct sp_inprocb;
struct sp_inprocc;

/*  This object (but not the msgqueues inside it) is synchronised using
    the inproc_ctx critical section. */

#define SP_MSGPIPE_PIPE0_ACTIVE 1
#define SP_MSGPIPE_PIPE1_ACTIVE 2

#define SP_MSGPIPE_EVENT_IN 1
#define SP_MSGPIPE_EVENT_OUT 2

struct sp_msgpipe {

    /*  The two ends of the pipe. First one is the bound end, second one
        is the connected end. */
    struct sp_pipebase pipes [2];

    /*  TODO: These flags are good only for asserting there's no bug in the
        library. Later on they should be removed. */
    int flags;

    /*  The two underlying queues to achieve bi-directional communication. */
    struct sp_msgqueue queues [2];

    /*  Incoming notifications. */
    struct sp_event inevents [2];
    struct sp_event outevents [2];

    /*  The pipe is owned by exactly one bound endpoint. */
    struct sp_list_item inprocb;

};

void sp_msgpipe_init (struct sp_msgpipe *self,
    struct sp_inprocb *inprocb, struct sp_inprocc *inprocc);
void sp_msgpipe_detachb (struct sp_msgpipe *self);
void sp_msgpipe_detachc (struct sp_msgpipe *self);

#endif
