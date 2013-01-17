/*
    Copyright (c) 2013 250bpm s.r.o.

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

#ifndef SP_CHUNKREF_INCLUDED
#define SP_CHUNKREF_INCLUDED

#define SP_CHUNKREF_MAX 32

#include "chunk.h"

#include <stdint.h>
#include <stddef.h>

struct sp_chunkref {
    uint8_t ref [SP_CHUNKREF_MAX];
};

void sp_chunkref_init (struct sp_chunkref *self, size_t size);
void sp_chunkref_init_chunk (struct sp_chunkref *self, struct sp_chunk *chunk);
void sp_chunkref_term (struct sp_chunkref *self);
void sp_chunkref_mv (struct sp_chunkref *dst, struct sp_chunkref *src);
void sp_chunkref_cp (struct sp_chunkref *dst, struct sp_chunkref *src);

void *sp_chunkref_data (struct sp_chunkref *self);
size_t sp_chunkref_size (struct sp_chunkref *self);

void sp_chunkref_trim (struct sp_chunkref *self, size_t n);

#endif

