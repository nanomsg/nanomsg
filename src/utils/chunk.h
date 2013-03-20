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

#ifndef NN_CHUNK_INCLUDED
#define NN_CHUNK_INCLUDED

#include "atomic.h"

#include <stdint.h>
#include <stddef.h>

struct nn_chunk;

struct nn_chunk_vfptr {
    void (*free) (void *p);
};

struct nn_chunk {

    /*  Fixed tag to distinguish chunks from random garbage. */
    uint32_t tag;

    /*  Distance between first actually allocated byte and first byte of
        nn_chunk structure. */
    uint32_t offset;

    /*  Number of places the chunk is referenced from. */
    struct nn_atomic refcount;

    /*  Size of the buffer in bytes. */
    size_t size;

    /*  Virtual functions. */
    const struct nn_chunk_vfptr *vfptr;

    /*  Actual message buffer follows the nn_chunk structure in the memory. */
};

/*  Allocates the chunk using the allocation mechanism specified by 'type'. */
struct nn_chunk *nn_chunk_alloc (size_t size, int type);

/*  Deallocates the chunk. */
void nn_chunk_free (struct nn_chunk *self);

/*  Increases the reference count of the chunk by 'n'. */
void nn_chunk_addref (struct nn_chunk *self, uint32_t n);

/*  Converts data pointer to chunk pointer. Returns NULL in the case of
    invalid chunk. */
struct nn_chunk *nn_chunk_from_data (void *data);

/*  Returns pointer to the chunk buffer. */
void *nn_chunk_data (struct nn_chunk *self);

/*  Returns size of the chunk buffer. */
size_t nn_chunk_size (struct nn_chunk *self);

/*  Trims n bytes from the beginning of the chunk. Returns pointer to the new
    chunk. */
struct nn_chunk *nn_chunk_trim (struct nn_chunk *self, size_t n);

#endif

