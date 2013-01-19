/*
    Copyright (c) 201-2013 250bpm s.r.o.

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

#ifndef SP_XRESPONDENT_INCLUDED
#define SP_XRESPONDENT_INCLUDED

#include "../../protocol.h"

#include "../../utils/excl.h"

extern struct sp_socktype *sp_xrespondent_socktype;

struct sp_xrespondent {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
};

void sp_xrespondent_init (struct sp_xrespondent *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
void sp_xrespondent_term (struct sp_xrespondent *self);

int sp_xrespondent_add (struct sp_sockbase *self, struct sp_pipe *pipe);
void sp_xrespondent_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrespondent_in (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrespondent_out (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrespondent_send (struct sp_sockbase *self, struct sp_msg *msg);
int sp_xrespondent_recv (struct sp_sockbase *self, struct sp_msg *msg);
int sp_xrespondent_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
int sp_xrespondent_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
int sp_xrespondent_sethdr (struct sp_msg *msg, const void *hdr, size_t hdrlen);
int sp_xrespondent_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen);

#endif
