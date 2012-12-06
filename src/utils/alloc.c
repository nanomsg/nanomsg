/*
    Copyright (c) 2012 250bpm s.r.o.

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

#include "alloc.h"

#if defined SP_ALLOC_MONITOR

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static size_t sp_alloc_bytes = 0;
static size_t sp_alloc_blocks = 0;

void *sp_alloc (size_t size)
{
    uint8_t *chunk;

    chunk = malloc (size + sizeof (size_t));
    if (!chunk)
        return NULL;
    *(size_t*) chunk = size;
    sp_alloc_bytes += size;
    ++sp_alloc_blocks;
    printf ("alloc %zu bytes (now there are %zu bytes allocated in %zu "
        "blocks)\n", size, sp_alloc_bytes, sp_alloc_blocks);
    return chunk + sizeof (size_t);
}

void *sp_realloc (void *ptr, size_t size)
{
    uint8_t *oldchunk;
    uint8_t *newchunk;
    size_t oldsize;

    oldchunk = (uint8_t*) (((size_t*) ptr) - 1);
    oldsize = *(size_t*) oldchunk;
    newchunk = realloc (oldchunk, size + sizeof (size_t));
    if (!newchunk)
        return NULL;
    *(size_t*) newchunk = size;
    sp_alloc_bytes -= oldsize;
    sp_alloc_bytes += size;
    printf ("realloc %zu bytes to %zu bytes (now there are %zu bytes "
        "allocated in %zu blocks)\n", oldsize, size, sp_alloc_bytes,
        sp_alloc_blocks);
    return newchunk + sizeof (size_t);
}

void sp_free (void *ptr)
{
    uint8_t *chunk;
    
    if (!ptr)
        return;
    chunk = (uint8_t*) (((size_t*) ptr) - 1);
    sp_alloc_bytes -= *(size_t*) chunk;
    --sp_alloc_blocks;
    printf ("free %zu bytes (now there are %zu bytes allocated in %zu "
        "blocks)\n", *(size_t*) chunk, sp_alloc_bytes, sp_alloc_blocks);
    free (chunk);
}

#else

#include <stdlib.h>

void *sp_alloc (size_t size)
{
    return malloc (size);
}

void *sp_realloc (void *ptr, size_t size)
{
    return realloc (ptr, size);
}

void sp_free (void *ptr)
{
    free (ptr);
}

#endif

