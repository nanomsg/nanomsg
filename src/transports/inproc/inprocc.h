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

#ifndef SP_INPROCC_INCLUDED
#define SP_INPROCC_INCLUDED

#include "../../transport.h"

#include "../../utils/list.h"

#include "msgpipe.h"

#define SP_INPROCC_NAMELEN_MAX 64

struct sp_inprocc {

    /*  This object is an endpoint. */
    struct sp_epbase epbase;

    /*  Name of the connected endpoint. */
    char addr [SP_INPROCC_NAMELEN_MAX];

    /*  This object is an element in the list of all connected endpoints managed
        by sp_inproc_ctx object. */
    struct sp_list_item list;

    /*  If the endpoint is connected this is the pointer to the associated
        message pipe. If it's not, the field is set to NULL. */
    struct sp_msgpipe *pipe;

};

int sp_inprocc_init (struct sp_inprocc *self, const char *addr, void *hint);
void sp_inprocc_add_pipe (struct sp_inprocc *self, struct sp_msgpipe *pipe);

#endif
