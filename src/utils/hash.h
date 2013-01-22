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

#ifndef NN_HASH_INCLUDED
#define NN_HASH_INCLUDED

#include <stdint.h>
#include <stddef.h>

#include "list.h"

struct nn_hash_item {
    uint32_t key;
    struct nn_list_item list;
};

struct nn_hash {
    uint32_t slots;
    uint32_t items;
    struct nn_list *array;
};

/*  Initialise the hash table. */
void nn_hash_init (struct nn_hash *self);

/*  Terminate the hash. Note that hash must be manually emptied before the
    termination. */
void nn_hash_term (struct nn_hash *self);

/*  Adds an item to the hash. */
void nn_hash_insert (struct nn_hash *self, uint32_t key,
    struct nn_hash_item *item);

/*  Removes the element from the hash it is in at the moment. */
void nn_hash_erase (struct nn_hash *self, struct nn_hash_item *item);

/*  Gets an item in the hash based on the key. */
struct nn_hash_item *nn_hash_get (struct nn_hash *self, uint32_t key);

#endif
