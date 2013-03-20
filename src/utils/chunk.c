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
#include "fast.h"
#include "err.h"

#include <string.h>

#define NN_CHUNK_TAG 0xdeadcafe

static void nn_chunk_default_free (void *p);
static const struct nn_chunk_vfptr nn_chunk_default_vfptr = {
    nn_chunk_default_free
};

struct nn_chunk *nn_chunk_alloc (size_t size, int type)
{
    size_t sz;
    struct nn_chunk *self;

    /*  Allocate the actual memory depending on the type. */
    sz = size + sizeof (struct nn_chunk);
    switch (type) {
    case 0:
        self = nn_alloc (sz, "message chunk");
        break;
    default:
        return NULL;
    }
    alloc_assert (self);

    /*  Fill in the chunk header. */
    self->tag = NN_CHUNK_TAG;
    self->offset = 0;
    nn_atomic_init (&self->refcount, 1);
    self->vfptr = &nn_chunk_default_vfptr;
    self->size = size;

    return self;
}

void nn_chunk_free (struct nn_chunk *self)
{
    nn_assert (self->tag == NN_CHUNK_TAG);

    /*  Decrement the reference count. Actual deallocation happens only if
        it drops to zero. */
    if (nn_atomic_dec (&self->refcount, 1) <= 1) {
        
        /*  Mark chunk as deallocated. */
        self->tag = 0;

        nn_atomic_term (&self->refcount);

        /*  Compute the beginning of the allocated block and deallocate it
            according to the allocation mechanism specified. */
        self->vfptr->free (((uint8_t*) self) - self->offset);
    }
}

void nn_chunk_addref (struct nn_chunk *self, uint32_t n)
{
    nn_assert (self->tag == NN_CHUNK_TAG);
    nn_atomic_inc (&self->refcount, n);
}

static void nn_chunk_default_free (void *p)
{
    nn_free (p);
}

struct nn_chunk *nn_chunk_from_data (void *data)
{
    struct nn_chunk *chunk;

    if (nn_slow (!data))
        return NULL;
    chunk = ((struct nn_chunk*) data) - 1;
    if (nn_slow (chunk->tag != NN_CHUNK_TAG))
        return NULL;
    return chunk;
}

void *nn_chunk_data (struct nn_chunk *self)
{
    return (void*) (self + 1);
}

size_t nn_chunk_size (struct nn_chunk *self)
{
    return self->size;
}

struct nn_chunk *nn_chunk_trim (struct nn_chunk *self, size_t n)
{
    struct nn_chunk *newself;

    /*  Sanity check. We cannot trim more bytes than there are in the chunk. */
    nn_assert (self->size >= n);

    /*  Move the chunk header to the new place. */
    newself = (struct nn_chunk*) (((uint8_t*) self) + n);
    memmove (newself, self, sizeof (struct nn_chunk));

    /*  Adjust the header. */
    newself->offset += n;
    newself->size -= n;

    return newself;
}

