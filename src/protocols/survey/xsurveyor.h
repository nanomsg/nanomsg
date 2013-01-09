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

#ifndef SP_XSURVEYOR_INCLUDED
#define SP_XSURVEYOR_INCLUDED

#include "../../protocol.h"

extern struct sp_socktype *sp_xsurveyor_socktype;

struct sp_xsurveyor_data {
    struct sp_pipe *pipe;
    struct sp_list_item outitem;
    struct sp_list_item initem;
};

struct sp_xsurveyor {

    /*  The generic socket base class. */
    struct sp_sockbase sockbase;

    /*  List of pipes to send messages to. */
    struct sp_list outpipes;

    /*  List of pipes that we can get messages from. */
    struct sp_list inpipes;

    /*  Next pipe to receive from. */
    struct sp_xsurveyor_data *current;

};

void sp_xsurveyor_init (struct sp_xsurveyor *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
void sp_xsurveyor_term (struct sp_xsurveyor *self);

int sp_xsurveyor_add (struct sp_sockbase *self, struct sp_pipe *pipe);
void sp_xsurveyor_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xsurveyor_in (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xsurveyor_out (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xsurveyor_send (struct sp_sockbase *self, const void *buf, size_t len);
int sp_xsurveyor_recv (struct sp_sockbase *self, void *buf, size_t *len);
int sp_xsurveyor_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
int sp_xsurveyor_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);

#endif

