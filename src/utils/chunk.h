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

#ifndef SP_CHUNK_INCLUDED
#define SP_CHUNK_INCLUDED

#include <stdint.h>
#include <stddef.h>

struct sp_chunk;

struct sp_chunk_vfptr {
    void (*free) (void *p);
};

struct sp_chunk {

    /*  Fixed tag to distinguish chunks from random garbage. */
    uint32_t tag;

    /*  Distance between first actually allocated byte and first byte of
        sp_chunk structure. */
    uint32_t offset;

    /*  Virtual functions. */
    const struct sp_chunk_vfptr *vfptr;

    /*  Size of the buffer in bytes. */
    size_t size;

    /*  Actual message buffer follows the sp_chunk structure in the memory. */
};

/*  Allocates the chunk using the allocation mechanism specified by 'type'. */
struct sp_chunk *sp_chunk_alloc (size_t size, int type);

/*  Deallocates the chunk. */
void sp_chunk_free (struct sp_chunk *self);

/*  Checks whether chunk is valid. */
int sp_chunk_check (struct sp_chunk *self);

/*  Returns pointer to the chunk buffer. */
void *sp_chunk_data (struct sp_chunk *self);

/*  Returns size of the chunk buffer. */
size_t sp_chunk_size (struct sp_chunk *self);

/*  Trims n bytes from the beginning of the chunk. Returns pointer to the new
    chunk. */
struct sp_chunk *sp_chunk_trim (struct sp_chunk *self, size_t n);

#endif

