/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.
    Copyright 2024 Staysail Systems, Inc.

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

#include "chunkref.h"
#include "err.h"

#include <string.h>

#define NN_CHUNKREF_EXT ((size_t)-1)

void nn_chunkref_init (struct nn_chunkref *self, size_t size)
{
    int rc;

    if (size <= NN_CHUNKREF_MAX) {
        self->size = size;
        return;
    }

    self->size = NN_CHUNKREF_EXT;
    rc = nn_chunk_alloc (size, 0, (void **)&self->u.chunk);
    errno_assert (rc == 0);
}

void nn_chunkref_init_chunk (struct nn_chunkref *self, void *chunk)
{
    self->size = NN_CHUNKREF_EXT;
    self->u.chunk = chunk;
}

void nn_chunkref_term (struct nn_chunkref *self)
{
    if (self->size == NN_CHUNKREF_EXT) {
        nn_chunk_free (self->u.chunk);
    }
}

void *nn_chunkref_getchunk (struct nn_chunkref *self)
{
    int rc;
    void *chunk;

    if (self->size == NN_CHUNKREF_EXT) {
        chunk = self->u.chunk;
        self->u.chunk = NULL;
        self->size = 0;
        return chunk;
    }

    nn_assert (self->size <= NN_CHUNKREF_MAX);
    rc = nn_chunk_alloc (self->size, 0, &chunk);
    errno_assert (rc == 0);
    memcpy (chunk, &self->u.ref, self->size);
    self->size = 0;
    return chunk;
}

void nn_chunkref_mv (struct nn_chunkref *dst, struct nn_chunkref *src)
{
    dst->size = src->size;
    if (src->size == NN_CHUNKREF_EXT) {
        dst->u.chunk = src->u.chunk;
    } else {
        nn_assert (src->size <= NN_CHUNKREF_MAX);
        memcpy (dst->u.ref, src->u.ref, src->size);
    }
}

void nn_chunkref_cp (struct nn_chunkref *dst, struct nn_chunkref *src)
{
    dst->size = src->size;
    if (src->size == NN_CHUNKREF_EXT) {
        nn_chunk_addref(src->u.chunk, 1);
        dst->u.chunk = src->u.chunk;
    } else {
        nn_assert (src->size <= NN_CHUNKREF_MAX);
        memcpy (dst->u.ref, src->u.ref, src->size);
    }
}

void *nn_chunkref_data (struct nn_chunkref *self)
{
    if (self->size > NN_CHUNKREF_MAX) {
        return self->u.chunk;
    } else {
        return self->u.ref;
    }
}

size_t nn_chunkref_size (struct nn_chunkref *self)
{
    if (self->size > NN_CHUNKREF_MAX) {
        return (nn_chunk_size(self->u.chunk));
    }
    return self->size;
}

void nn_chunkref_trim (struct nn_chunkref *self, size_t n)
{
    if (self->size == NN_CHUNKREF_EXT) {
        nn_chunk_trim (self->u.chunk, n);
        return;
    }

    nn_assert (self->size >= n);
    nn_assert (self->size <= NN_CHUNKREF_MAX);
    memmove (self->u.ref, self->u.ref + n, self->size - n);
    self->size -= n;
}

void nn_chunkref_bulkcopy_start (struct nn_chunkref *self, uint32_t copies)
{
    if (self->size == NN_CHUNKREF_EXT) {
        nn_chunk_addref (self->u.chunk, copies);
    }
}

void nn_chunkref_bulkcopy_cp (struct nn_chunkref *dst, struct nn_chunkref *src)
{
    *dst = *src;
}
