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

#include "../../aio/aio.h"

#include "../../utils/mutex.h"

struct nn_inprocb;
struct nn_inprocc;

#define NN_MSGPIPEHALF_FLAG_DETACHING 1

struct nn_msgpipehalf {

    /*  Base class to integrate the pipe with the rest of nanomsg
        infrastructure. */
    struct nn_pipebase pipebase;

    /*  Any combination of the flags defined above. */
    int flags;

    /*  Message queue that this side of the pipe receives from. */
    struct nn_msgqueue queue;

    /*  Event sink. */
    const struct nn_cp_sink *sink;

    /*  Various inbound events. */
    struct nn_event inevent;
    struct nn_event outevent;
    struct nn_event detachevent;

    /*  Function from removing the pipe from the endpoint. */
    void (*rmpipefn) (struct nn_msgpipehalf *self);
};

#define NN_MSGPIPE_FLAG_BHALF_DEAD 1
#define NN_MSGPIPE_FLAG_CHALF_DEAD 2

struct nn_msgpipe {

    /*  Critical section to guard the whole msgpipe object. */
    struct nn_mutex sync;

    /*  Any combination of the flags defined above. */
    int flags;

    /*  Two halfs of the pipe (bind side and connect side). */
    struct nn_msgpipehalf bhalf;
    struct nn_msgpipehalf chalf;

    /*  The pipe is owned by exactly one bound endpoint. */
    struct nn_list_item item;

    /*  Bound-side and connected-side endpoint. */
    struct nn_inprocb *inprocb;
    struct nn_inprocc *inprocc;
};

/*  Initialise the message pipe. */
void nn_msgpipe_init (struct nn_msgpipe *self,
    struct nn_inprocb *inprocb, struct nn_inprocc *inprocc);

/*  This function is called from the bound side to ask the pipe to terminate.
    Accomplishment of the task is signaled by invoking nn_inprocb_rm_pipe. */
void nn_msgpipe_detachb (struct nn_msgpipe *self);

/*  This function is called from the connected side to ask the pipe to
    terminate. Accomplishment of the task is signaled by invoking
    nn_inprocc_rm_pipe. */
void nn_msgpipe_detachc (struct nn_msgpipe *self);

#endif
