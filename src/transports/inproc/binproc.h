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

#ifndef NN_BINPROC_INCLUDED
#define NN_BINPROC_INCLUDED

#include "../../transport.h"

#include "../../aio/fsm.h"

#include "../../utils/list.h"

struct nn_cinproc;

struct nn_binproc {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is an endpoint. */
    struct nn_epbase epbase;

    /*  The list of inproc sessions owned by this object. */
    struct nn_list sinprocs;

    /*  This object is an element in the list of all bound endpoints managed
        by nn_inproc object. */
    struct nn_list_item item;

    /*  Number of connects underway. We cannot deallocate this object until
        the value drops to zero. Note that this member is sychronised using
        inproc obejct's global critical section. */
    int connects;
};

struct nn_binproc *nn_binproc_create (void *hint);

const char *nn_binproc_getaddr (struct nn_binproc *self);
void nn_binproc_connect (struct nn_binproc *self, struct nn_cinproc *peer);

#endif
