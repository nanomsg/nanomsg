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

#include "xrespondent.h"

#include "../../sp.h"
#include "../../survey.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"

/*  Implementation of sp_sockbase's virtual functions. */
static const struct sp_sockbase_vfptr sp_xrespondent_sockbase_vfptr = {
    sp_xrespondent_term,
    sp_xrespondent_add,
    sp_xrespondent_rm,
    sp_xrespondent_in,
    sp_xrespondent_out,
    sp_xrespondent_send,
    sp_xrespondent_recv,
    sp_xrespondent_setopt,
    sp_xrespondent_getopt
};

void sp_xrespondent_init (struct sp_xrespondent *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
}

void sp_xrespondent_term (struct sp_sockbase *self)
{
    struct sp_xrespondent *xrespondent;

    xrespondent = sp_cont (self, struct sp_xrespondent, sockbase);

    sp_excl_term (&xrespondent->excl);
}

int sp_xrespondent_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_xrespondent, sockbase)->excl,
        pipe);
}

void sp_xrespondent_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_xrespondent, sockbase)->excl, pipe);
}

int sp_xrespondent_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_xrespondent, sockbase)->excl,
        pipe);
}

int sp_xrespondent_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_xrespondent, sockbase)->excl,
        pipe);
}

int sp_xrespondent_send (struct sp_sockbase *self, const void *buf,
    size_t len)
{
    return sp_excl_send (&sp_cont (self, struct sp_xrespondent, sockbase)->excl,
        buf, len);
}

int sp_xrespondent_recv (struct sp_sockbase *self, void *buf,
    size_t *len)
{
    struct sp_xrespondent *xrespondent;

    xrespondent = sp_cont (self, struct sp_xrespondent, sockbase);

    return sp_excl_recv (&xrespondent->excl, buf, len);
}

int sp_xrespondent_setopt (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    return -ENOPROTOOPT;
}

int sp_xrespondent_getopt (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_xrespondent_create (int fd)
{
    struct sp_xrespondent *self;

    self = sp_alloc (sizeof (struct sp_xrespondent), "socket (xrespondent)");
    alloc_assert (self);
    sp_xrespondent_init (self, &sp_xrespondent_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_xrespondent_socktype_struct = {
    AF_SP_RAW,
    SP_RESPONDENT,
    sp_xrespondent_create
};

struct sp_socktype *sp_xrespondent_socktype = &sp_xrespondent_socktype_struct;

