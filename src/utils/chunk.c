/*
    Copyright (c) 2013 Martin Sustrik  All rights reserved.
    Copyright (c) 2014 Achille Roussel All rights reserved.
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

#include "chunk.h"
#include "atomic.h"
#include "alloc.h"
#include "fast.h"
#include "wire.h"
#include "err.h"

#include <string.h>

#define NN_CHUNK_TAG 0xdeadcafe
#define NN_CHUNK_TAG_PTR 0xdeadd00d
#define NN_CHUNK_TAG_DEALLOCATED 0xbeadfeed

/* Helper functions to access properties out of the structure */
#define NN_CHUNK_TAG_OFFSET(p) (uint8_t*) p - sizeof (uint32_t)
#define NN_CHUNK_SIZE_OFFSET(p) (uint8_t*) p - 2 * sizeof (uint32_t)

/* Last assigned chunk id */
uint32_t nn_last_chunk_id = 0;

struct nn_chunk {

    /*  Number of places the chunk is referenced from. */
    struct nn_atomic refcount;

    /*  Size of the message in bytes. */
    size_t size;

    /* Unique ID of the chunk, used by memory */
    uint32_t id;

    /*  Deallocation function. */
    nn_chunk_free_fn ffn;

    /*  The structure if followed by optional empty space, a 32 bit unsigned
        integer specifying the size of said empty space, a 32 bit tag and
        the message data itself. */
};

/*  This structure is used in place of data when the nn_chunk is keeping
    the reference to a user pointer, constructed with nn_chunk_alloc_ptr. */
struct nn_chunk_ptr {

    /* The pointer to the data being addressed */
    void * ptr;

    /* The user-provided destructor function */
    nn_chunk_free_fn destructor;

};

/*  Private functions. */
static struct nn_chunk *nn_chunk_getptr (void *p);
static void *nn_chunk_getdata (struct nn_chunk *c);
static void nn_chunk_default_free (void *p);
static size_t nn_chunk_hdrsize ();

static int nn_chunk_new (size_t size, int type, uint32_t tag, 
    struct nn_chunk ** result)
{
    size_t sz;
    struct nn_chunk *self;
    const size_t hdrsz = nn_chunk_hdrsize ();

    /*  Compute total size to be allocated. Check for overflow. */
    sz = hdrsz + size;
    if (nn_slow (sz < hdrsz))
        return -ENOMEM;

    /*  Allocate the actual memory depending on the type. */
    switch (type) {
    case 0:
        self = nn_alloc (sz, "message chunk");
        break;
    default:
        return -EINVAL;
    }
    if (nn_slow (!self))
        return -ENOMEM;

    /*  Fill in the chunk header. */
    nn_atomic_init (&self->refcount, 1);
    self->size = size;
    self->id = nn_last_chunk_id++;
    self->ffn = nn_chunk_default_free;

    /*  Fill in the size of the empty space between the chunk header
        and the message. */
    nn_putl ((uint8_t*) ((uint32_t*) (self + 1)), 0);

    /*  Fill in the tag. */
    nn_putl ((uint8_t*) ((((uint32_t*) (self + 1))) + 1), tag);

    /* Sucess */
    *result = self;
    return 0;
}

int nn_chunk_alloc (size_t size, int type, void **result)
{
    int ret;
    struct nn_chunk *self;

    /* Create new chunk */
    ret = nn_chunk_new( size, type, NN_CHUNK_TAG, &self );
    if (ret)
        return ret;

    /* Update result */
    *result = nn_chunk_getdata (self);
    return 0;
}

int nn_chunk_alloc_ptr ( void * data, size_t size, nn_chunk_free_fn destructor, 
    void **result)
{
    int ret;
    struct nn_chunk_ptr *ptr_chunk;
    struct nn_chunk *self;

    /* Create new user-pointer chunk */
    ret = nn_chunk_new(sizeof(struct nn_chunk_ptr), 0, NN_CHUNK_TAG_PTR, &self);
    if (ret)
        return ret;

    /* Re-use size field since it's otherwise useless, since the size
       of the data we carry is always `sizeof(void *)` */
    self->size = size;

    /* Update chunk pointer properties */
    ptr_chunk = (struct nn_chunk_ptr *) nn_chunk_getdata (self);
    ptr_chunk->ptr = data;
    ptr_chunk->destructor = destructor;

    /* Update result */
    *result = ptr_chunk;
    return 0;
}

void *nn_chunk_deref ( void *p)
{
    /* Prioritize de-referencing of pointer */
    if (nn_fast( nn_getl( NN_CHUNK_TAG_OFFSET(p) ) == NN_CHUNK_TAG_PTR )) {
        return ((struct nn_chunk_ptr *) p)->ptr;
    } else {
        return p;
    }
}

int nn_chunk_realloc (size_t size, void **chunk)
{
    struct nn_chunk *self;
    struct nn_chunk *new_chunk;
    void *new_ptr;
    size_t hdr_size;
    size_t new_size;
    int rc;

    /* We cannot re-alloc user-pointers */
    if (nn_slow( nn_getl(NN_CHUNK_TAG_OFFSET(*chunk)) == NN_CHUNK_TAG_PTR ))
        return -EINVAL;

    self = nn_chunk_getptr (*chunk);

    /*  Check if we only have one reference to this object, in that case we can
        reallocate the memory chunk. */
    if (self->refcount.n == 1) {

        /* Compute new size, check for overflow. */
        hdr_size = nn_chunk_hdrsize ();
        new_size = hdr_size + size;
        if (nn_slow (new_size < hdr_size))
            return -ENOMEM;

        /*  Reallocate memory chunk. */
        new_chunk = nn_realloc (self, new_size);
        if (nn_slow (new_chunk == NULL))
            return -ENOMEM;

        new_chunk->size = size;
        *chunk = nn_chunk_getdata (new_chunk);
    }

    /*  There are many references to this memory chunk, we have to create a new
        one and copy the data. */
    else {
        new_ptr = NULL;
        rc = nn_chunk_alloc (size, 0, &new_ptr);

        if (nn_slow (rc != 0)) {
            return rc;
        }

        memcpy (new_ptr, nn_chunk_getdata (self), self->size);
        *chunk = new_ptr;
        nn_atomic_dec (&self->refcount, 1);
    }

    return 0;
}

void nn_chunk_free (void *p)
{
    struct nn_chunk *self;
    size_t off;

    self = nn_chunk_getptr (p);

    /*  Decrement the reference count. Actual deallocation happens only if
        it drops to zero. */
    if (nn_atomic_dec (&self->refcount, 1) <= 1) {

        /* Call user destructor when using user-pointers */
        if ( nn_getl(NN_CHUNK_TAG_OFFSET(p)) == NN_CHUNK_TAG_PTR ) {

            /* Get base address of user pointer without offset */
            off = nn_getl( NN_CHUNK_SIZE_OFFSET(p) );
            ((struct nn_chunk_ptr *) p)->destructor(
                    (uint8_t*)((struct nn_chunk_ptr *) p)->ptr - off
                );
        }

        /*  Mark chunk as deallocated. */
        nn_putl ((uint8_t*) (((uint32_t*) p) - 1), NN_CHUNK_TAG_DEALLOCATED);

        /*  Deallocate the resources held by the chunk. */
        nn_atomic_term (&self->refcount);

        /*  Deallocate the memory block according to the allocation
            mechanism specified. */
        self->ffn (self);
    }
}

void nn_chunk_addref (void *p, uint32_t n)
{
    struct nn_chunk *self;

    self = nn_chunk_getptr (p);

    nn_atomic_inc (&self->refcount, n);
}

size_t nn_chunk_size (void *p)
{
    return nn_chunk_getptr (p)->size;
}

void *nn_chunk_trim (void *p, size_t n)
{
    struct nn_chunk *self;
    const size_t hdrsz = sizeof (struct nn_chunk) + 2 * sizeof (uint32_t);
    size_t empty_space;

    self = nn_chunk_getptr (p);

    /*  Sanity check. We cannot trim more bytes than there are in the chunk. */
    nn_assert (n <= self->size);

    /* In case of user pointer just move the user pointer forward */
    if ( nn_getl( NN_CHUNK_TAG_OFFSET(p) ) == NN_CHUNK_TAG_PTR ) {

        /* Increase empty space defined in the header */
        empty_space = nn_getl( NN_CHUNK_SIZE_OFFSET(p) );
        empty_space += n;

        /* Forward user pointer */
        ((struct nn_chunk_ptr *) p)->ptr =
            ((uint8_t*) ((struct nn_chunk_ptr *) p)->ptr) + n;

        /* Update header */
        nn_putl( NN_CHUNK_SIZE_OFFSET(p), empty_space );

    } else {

        /*  Adjust the chunk header. */
        p = ((uint8_t*) p) + n;
        nn_putl ((uint8_t*) (((uint32_t*) p) - 1), NN_CHUNK_TAG);
        empty_space = (uint8_t*) p - (uint8_t*) self - hdrsz;
        nn_assert(empty_space < UINT32_MAX);
        nn_putl ( NN_CHUNK_SIZE_OFFSET(p), (uint32_t) empty_space);

    }

    /*  Adjust the size of the message. */
    self->size -= n;

    return p;
}

int nn_chunk_describe(void *p, struct nn_chunk_desc *d)
{
    uint32_t tag;
    uint32_t off;
    struct nn_chunk * chunk;

    /* Validate tag */
    tag = nn_getl(NN_CHUNK_TAG_OFFSET(p));
    nn_assert ( (tag == NN_CHUNK_TAG) || (tag == NN_CHUNK_TAG_PTR) );

    /* Get offset */
    off = nn_getl( NN_CHUNK_SIZE_OFFSET(p) );
    chunk = (struct  nn_chunk*) ((uint8_t*) p - 2 *sizeof (uint32_t) - off -
        sizeof (struct nn_chunk));

    /* Get ID */
    d->id = chunk->id;

    /* Get base address to the data no matter what's the white space */
    if ( tag == NN_CHUNK_TAG_PTR ) {
        d->base = ((uint8_t*) ((struct nn_chunk_ptr *) p)->ptr) - off;
        d->len = chunk->size + off;
    } else {
        d->base = ((uint8_t*) p) - off;
        d->len = chunk->size + off;
    }

    /* Success */
    return 0;
}

static struct nn_chunk *nn_chunk_getptr (void *p)
{
    uint32_t tag;
    uint32_t off;

    tag = nn_getl(NN_CHUNK_TAG_OFFSET(p));
    nn_assert ( (tag == NN_CHUNK_TAG) || (tag == NN_CHUNK_TAG_PTR) );

    /* On user-pointer chunks the offset is virtual */
    if (tag == NN_CHUNK_TAG_PTR) {
        off = 0;
    } else {
        off = nn_getl( NN_CHUNK_SIZE_OFFSET(p) );
    }

    return (struct  nn_chunk*) ((uint8_t*) p - 2 *sizeof (uint32_t) - off -
        sizeof (struct nn_chunk));
}

static void *nn_chunk_getdata (struct nn_chunk *self)
{
    return ((uint8_t*) (self + 1)) + 2 * sizeof (uint32_t);
}

static void nn_chunk_default_free (void *p)
{
    nn_free (p);
}

static size_t nn_chunk_hdrsize ()
{
    return sizeof (struct nn_chunk) + 2 * sizeof (uint32_t);
}

