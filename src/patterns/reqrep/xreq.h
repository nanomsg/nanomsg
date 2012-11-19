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

#ifndef SP_XREQ_INCLUDED
#define SP_XREQ_INCLUDED

#include "../../pattern.h"

#include "../../utils/excl.h"

struct sp_xreq {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
};

void sp_xreq_init (struct sp_xreq *self, const struct sp_sockbase_vfptr *vfptr,
    int fd);

void sp_xreq_term (struct sp_sockbase *self);
int sp_xreq_add (struct sp_sockbase *self, struct sp_pipe *pipe);
void sp_xreq_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xreq_in (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xreq_out (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xreq_send (struct sp_sockbase *self, const void *buf, size_t len);
int sp_xreq_recv (struct sp_sockbase *self, void *buf, size_t *len);
int sp_xreq_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
int sp_xreq_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
void sp_xreq_timeout (struct sp_sockbase *self, struct sp_timer_hndl *hndl);

extern struct sp_socktype *sp_xreq_socktype;

#endif
