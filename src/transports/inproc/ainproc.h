/*
    Copyright (c) 2013 250bpm s.r.o.

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

#ifndef NN_AINPROC_INCLUDED
#define NN_AINPROC_INCLUDED

#include "msgpipe.h"

#include "../../transport.h"

#include "../../aio/fsm.h"

#include "../../utils/list.h"

#define NN_AINPROC_STOPPED 1

struct nn_ainproc {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  Local end of the message pipe. */
    struct nn_msgpipe local;

    /*  Remote end of the message pipe. */
    struct nn_msgpipe *remote;

    /*  binproc object has a list of associated ainproc objects. */
    struct nn_list_item item;
};

void nn_ainproc_init (struct nn_ainproc *self, struct nn_fsm *owner);
void nn_ainproc_term (struct nn_ainproc *self);

void nn_ainproc_start (struct nn_ainproc *self);
void nn_ainproc_stop (struct nn_ainproc *self);

#endif

