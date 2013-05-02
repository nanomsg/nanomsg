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

#ifndef NN_BSTREAM_INCLUDED
#define NN_BSTREAM_INCLUDED

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/list.h"

/*  Bound stream socket. */

struct nn_bstream;
struct nn_astream;

/*  Virtual functions to be implemented by the specific stream type. */
struct nn_bstream_vfptr {
    int (*open) (const char *addr, struct nn_usock *usock,
        struct nn_fsm *owner);
};

struct nn_bstream {

    /*  State machine. */
    struct nn_fsm fsm;
    int state;

    /*  Virual functions to access specific transport type. */
    const struct nn_bstream_vfptr *vfptr;

    /*  This object is an endpoint. */
    struct nn_epbase epbase;

    /*  The listening socket. */
    struct nn_usock usock;

    /*  New connection being accepted at the moment. */
    struct nn_astream *astream;

    /*  List of all sockets accepted via this endpoint. */
    struct nn_list astreams;
};

int nn_bstream_create (const struct nn_bstream_vfptr *vfptr, void *hint,
    struct nn_epbase **epbase);

#endif
