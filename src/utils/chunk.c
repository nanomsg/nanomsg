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

#include "chunk.h"
#include "alloc.h"
#include "err.h"

#include <string.h>

#define SP_CHUNK_TAG 0xdeadcafe

static void sp_chunk_default_free (void *p);
static const struct sp_chunk_vfptr sp_chunk_default_vfptr = {
    sp_chunk_default_free
};

struct sp_chunk *sp_chunk_alloc (size_t size, int type)
{
    size_t sz;
    struct sp_chunk *self;

    /*  Allocate the actual memory depending on the type. */
    sz = size + sizeof (struct sp_chunk);
    switch (type) {
    case 0:
        self = sp_alloc (sz, "message chunk");
    default:
        return NULL;
    }
    alloc_assert (self);

    /*  Fill in the chunk header. */
    self->tag = SP_CHUNK_TAG;
    self->offset = 0;
    self->vfptr = &sp_chunk_default_vfptr;
    self->size = size;

    return self;
}

void sp_chunk_free (struct sp_chunk *self)
{
    /*  Compute the beginning of the allocated block and deallocate it
        according to the allocation mechanism specified. */
    self->vfptr->free (((uint8_t*) self) - self->offset);
}

static void sp_chunk_default_free (void *p)
{
    sp_free (p);
}

void *sp_chunk_data (struct sp_chunk *self)
{
    return (void*) (self + 1);
}

size_t sp_chunk_size (struct sp_chunk *self)
{
    return self->size;
}

struct sp_chunk *sp_chunk_trim (struct sp_chunk *self, size_t n)
{
    struct sp_chunk *newself;

    /*  Sanity check. We cannot trim more bytes than there are in the chunk. */
    sp_assert (self->size >= n);

    /*  Move the chunk header to the new place. */
    newself = (struct sp_chunk*) (((uint8_t*) self) + n);
    memmove (newself, self, sizeof (struct sp_chunk));

    /*  Adjust the header. */
    newself->offset += n;
    newself->size -= n;

    return newself;
}

