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

/*  Signature of the chunk deallocator function */
typedef void (*nn_chunk_free_fn) (void *p, void *user);

/*  Allocates the chunk using the allocation mechanism specified by 'type'. */
int nn_chunk_alloc (size_t size, int type, void **result);

/*  Allocates the chunk with existing data destructor callback
    WARNING: This should *NOT* be used by transports or protocols to allocate 
             messages for the recv() end of the transport! The user will not
             be able to free your message otherwise! */
int nn_chunk_alloc_ptr ( void * data, size_t size, nn_chunk_free_fn destructor, 
    void *userptr, void **result);

/*  Dereference the chunk if this is a pointer chunk, otherwise return p */
void *nn_chunk_deref (void *p);

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

/*  The following functions are dangerous and should only be used if you
    absolutely know what you are doing */

/*  Replaces the chunk's free function with the one provided. It returns
    the previous free function.

    This is useful by some transports, such as OFI, in order to keep track
    of the de-allocated chunks, and release the related resources. 

    This function returns 0 if the function was successfuly replaced,
    or -EINVAL if the given function pointer is already defined. */
int nn_chunk_replace_free_fn(void *p, nn_chunk_free_fn new_fn,void *new_ffnptr,
    nn_chunk_free_fn * old_fn, void **old_ffnptr);

/*  Reset chunk properties (ex. previous trims) and set the 'size' property of 
    the chunk, without reallocating. This is helpful if you want to achieve 
    zero-copy by pre-allocating a couple of message and always re-using them.

    If you only want to undo previous calls to nn_chunk_trim, without affecting
    the original size of the chunk, set size=0.

    It is VERY DANGEROUS if you try to change the size of the chunk
    into something bigger than it was originally allocated. That said
    you are on your own! */
void* nn_chunk_reset(void *p, size_t size);

#endif

