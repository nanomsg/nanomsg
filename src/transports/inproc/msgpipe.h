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
#include "../../utils/mutex.h"

#define NN_MSGPIPEHALF_STATE_ATTACHED 1
#define NN_MSGPIPEHALF_STATE_DETACHING 2
#define NN_MSGPIPEHALF_STATE_DETACHED 3 

struct nn_msgpipehalf {

    /*  Base class to integrate the pipe with the rest of nanomsg
        infrastructure. */
    struct nn_pipebase pipebase;

    /*  One of the states defined above. */
    int state;

    /*  Message queue that this side of the pipe receives from. */
    struct nn_msgqueue queue;

    /*  Event sink. */
    const struct nn_cp_sink *sink;

    /*  Various inbound events. */
    struct nn_event inevent;
    struct nn_event outevent;
    struct nn_event detachevent;
};

struct nn_msgpipe {

    /*  Critical section to guard the whole msgpipe object. */
    struct nn_mutex sync;

    /*  Two halfs of the pipe (bind side and connect side). */
    struct nn_msgpipehalf bhalf;
    struct nn_msgpipehalf chalf;

    /*  The pipe is owned by exactly one bound endpoint. */
    struct nn_list_item inprocb;
};

void nn_msgpipe_init (struct nn_msgpipe *self,
    struct nn_epbase *inprocb, struct nn_epbase *inprocc);

void nn_msgpipe_detachb (struct nn_msgpipe *self);
void nn_msgpipe_detachc (struct nn_msgpipe *self);

#endif
