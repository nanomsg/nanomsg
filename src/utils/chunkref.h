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

#ifndef NN_CHUNKREF_INCLUDED
#define NN_CHUNKREF_INCLUDED

#define NN_CHUNKREF_MAX 32

#include "chunk.h"

#include <stdint.h>
#include <stddef.h>

struct nn_chunkref {
    uint8_t ref [NN_CHUNKREF_MAX];
};

void nn_chunkref_init (struct nn_chunkref *self, size_t size);
void nn_chunkref_init_chunk (struct nn_chunkref *self, struct nn_chunk *chunk);
void nn_chunkref_term (struct nn_chunkref *self);
struct nn_chunk *nn_chunkref_getchunk (struct nn_chunkref *self);
void nn_chunkref_mv (struct nn_chunkref *dst, struct nn_chunkref *src);
void nn_chunkref_cp (struct nn_chunkref *dst, struct nn_chunkref *src);

void *nn_chunkref_data (struct nn_chunkref *self);
size_t nn_chunkref_size (struct nn_chunkref *self);

void nn_chunkref_trim (struct nn_chunkref *self, size_t n);

#endif

