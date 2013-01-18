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

#ifndef SP_EXCL_INCLUDED
#define SP_EXCL_INCLUDED

#include "../protocol.h"

#include <stddef.h>

/*  This is an object to handle a single pipe. To be used by socket types that
    can work with precisely one connection, e.g. PAIR. */

struct sp_excl {

    /*  The pipe being used at the moment. All other pipes will be rejected
        until this one terminates. NULL if there is no connected pipe. */
    struct sp_pipe *pipe;

    /*  Pipe ready for receiving. It's either equal to 'pipe' or NULL. */
    struct sp_pipe *inpipe;

    /*  Pipe ready for sending. It's either equal to 'pipe' or NULL. */
    struct sp_pipe *outpipe;

};

void sp_excl_init (struct sp_excl *self);
void sp_excl_term (struct sp_excl *self);
int sp_excl_add (struct sp_excl *self, struct sp_pipe *pipe);
void sp_excl_rm (struct sp_excl *self, struct sp_pipe *pipe);
int sp_excl_in (struct sp_excl *self, struct sp_pipe *pipe);
int sp_excl_out (struct sp_excl *self, struct sp_pipe *pipe);
int sp_excl_send (struct sp_excl *self, struct sp_msg *msg);
int sp_excl_recv (struct sp_excl *self, struct sp_msg *msg);

#endif
