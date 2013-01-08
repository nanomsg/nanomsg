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

#ifndef SP_XREP_INCLUDED
#define SP_XREP_INCLUDED

#include "../../pattern.h"

#include "../../utils/list.h"
#include "../../utils/hash.h"

#include <stddef.h>
#include <stdint.h>

#define SP_XREP_OUT 1

struct sp_xrep_data {
    struct sp_pipe *pipe;
    struct sp_hash_item pipes;
    struct sp_list_item inpipes;
    uint32_t flags;
};

struct sp_xrep {

    struct sp_sockbase sockbase;

    /*  Key to be assigned to the next added pipe. */
    uint32_t next_key;

    /*  Map of all registered pipes indexed by the peer ID. */
    struct sp_hash pipes;

    /*  List of all pipes that can be read from. */
    struct sp_list inpipes;

    /*  Next inpipe to use to receive a message. */
    struct sp_xrep_data *current;

};

void sp_xrep_init (struct sp_xrep *self, const struct sp_sockbase_vfptr *vfptr,
    int fd);
void sp_xrep_term (struct sp_xrep *self);

int sp_xrep_add (struct sp_sockbase *self, struct sp_pipe *pipe);
void sp_xrep_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrep_in (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrep_out (struct sp_sockbase *self, struct sp_pipe *pipe);
int sp_xrep_send (struct sp_sockbase *self, const void *buf, size_t len);
int sp_xrep_recv (struct sp_sockbase *self, void *buf, size_t *len);
int sp_xrep_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
int sp_xrep_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);

extern struct sp_socktype *sp_xrep_socktype;

#endif
