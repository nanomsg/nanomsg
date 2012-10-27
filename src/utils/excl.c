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

#include "excl.h"

#include "fast.h"
#include "err.h"

void sp_excl_init (struct sp_excl *self)
{
    self->pipe = NULL;
    self->inpipe = NULL;
    self->outpipe = NULL;
}

void sp_excl_term (struct sp_excl *self)
{
    sp_assert (!self->pipe);
    sp_assert (!self->inpipe);
    sp_assert (!self->outpipe);
}

int sp_excl_add (struct sp_excl *self, struct sp_pipe *pipe)
{
    /*  If there's a connection being used, reject any new connection. */
    if (self->pipe)
        return -EISCONN;

    /*  Remember that this pipe is the active one. */
    self->pipe = pipe;

    return 0;
}

void sp_excl_rm (struct sp_excl *self, struct sp_pipe *pipe)
{
   sp_assert (self->pipe);
   self->pipe = NULL;
   self->inpipe = NULL;
   self->outpipe = NULL;
}

int sp_excl_in (struct sp_excl *self, struct sp_pipe *pipe)
{
    sp_assert (!self->inpipe);
    sp_assert (pipe == self->pipe);
    self->inpipe = pipe;
    return 1;
}

int sp_excl_out (struct sp_excl *self, struct sp_pipe *pipe)
{
    sp_assert (!self->outpipe);
    sp_assert (pipe == self->pipe);
    self->outpipe = pipe;
    return 1;
}

int sp_excl_send (struct sp_excl *self, const void *buf, size_t len)
{
    int rc;

    if (sp_slow (!self->outpipe))
        return -EAGAIN;

    rc = sp_pipe_send (self->outpipe, buf, len);
    errnum_assert (rc >= 0, -rc);

    if (rc & SP_PIPE_RELEASE)
        self->outpipe = NULL;

    return 0;
}

int sp_excl_recv (struct sp_excl *self, void *buf, size_t *len)
{
    int rc;

    if (sp_slow (!self->inpipe))
        return -EAGAIN;

    rc = sp_pipe_recv (self->inpipe, buf, len);
    errnum_assert (rc >= 0, -rc);

    if (rc & SP_PIPE_RELEASE)
        self->inpipe = NULL;

    return 0;
}

