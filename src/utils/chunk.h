/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.
    Copyright (c) 2016 Ioannis Charalampidis All rights reserved.

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

#include <stddef.h>
#include "int.h"

/* Chunk description used by memory management utilities.
   This is intented to be used by the OFI transport for memory registration. */
struct nn_chunk_desc {

    /* Base pointer to the data buffer */
    void * base;

    /* Length of the buffer */
    size_t len;

    /* ID of the buffer */
    uint32_t id;

};

/*  Signature of the chunk deallocator function */
typedef void (*nn_chunk_free_fn) (void *p);

/*  Allocates the chunk using the allocation mechanism specified by 'type'. */
int nn_chunk_alloc (size_t size, int type, void **result);

/*  Allocates the chunk with existing data destructor callback */
int nn_chunk_alloc_ptr ( void * data, size_t size, nn_chunk_free_fn destructor, 
    void **result);

/*  Dereference the chunk if this is a pointer chunk, otherwise return p */
void *nn_chunk_deref (void *p);

/*  Get a description of the specified chunk */
int nn_chunk_describe(void *p, struct nn_chunk_desc *d);

/*  Resizes a chunk previously allocated with nn_chunk_alloc. */
int nn_chunk_realloc (size_t size, void **chunk);

/*  Releases a reference to the chunk and once the reference count had dropped
    to zero, deallocates the chunk. */
void nn_chunk_free (void *p);

/*  Increases the reference count of the chunk by 'n'. */
void nn_chunk_addref (void *p, uint32_t n);

/*  Returns size of the chunk buffer. */
size_t nn_chunk_size (void *p);

/*  Trims n bytes from the beginning of the chunk. Returns pointer to the new
    chunk. */
void *nn_chunk_trim (void *p, size_t n);

#endif

