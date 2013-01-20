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

#include "msg.h"

#include <string.h>

void sp_msg_init (struct sp_msg *self, size_t size)
{
    sp_chunkref_init (&self->hdr, 0);
    sp_chunkref_init (&self->body, size);
}

void sp_msg_init_chunk (struct sp_msg *self, struct sp_chunk *chunk)
{
    sp_chunkref_init (&self->hdr, 0);
    sp_chunkref_init_chunk (&self->body, chunk);
}

void sp_msg_term (struct sp_msg *self)
{
    sp_chunkref_term (&self->hdr);
    sp_chunkref_term (&self->body);
}

void sp_msg_mv (struct sp_msg *dst, struct sp_msg *src)
{
    sp_chunkref_mv (&dst->hdr, &src->hdr);
    sp_chunkref_mv (&dst->body, &src->body);
}

void sp_msg_cp (struct sp_msg *dst, struct sp_msg *src)
{
    sp_chunkref_cp (&dst->hdr, &src->hdr);
    sp_chunkref_cp (&dst->body, &src->body);
}


