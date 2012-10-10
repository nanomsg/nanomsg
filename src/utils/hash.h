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

#ifndef SP_HASH_INCLUDED
#define SP_HASH_INCLUDED

#include <stdint.h>
#include <stddef.h>

#include "list.h"

struct sp_hash_item {
    uint32_t key;
    struct sp_list_item list;
};

struct sp_hash {
    uint32_t slots;
    uint32_t items;
    struct sp_list *array;
};

/*  Initialise the hash table. */
void sp_hash_init (struct sp_hash *self);

/*  Terminate the hash. Note that hash must be manually emptied before the
    termination. */
void sp_hash_term (struct sp_hash *self);

/*  Adds an item to the hash. */
void sp_hash_insert (struct sp_hash *self, uint32_t key,
    struct sp_hash_item *item);

/*  Removes the element from the hash it is in at the moment. */
void sp_hash_erase (struct sp_hash *self, struct sp_hash_item *item);

/*  Gets an item in the hash based on the key. */
struct sp_hash_item *sp_hash_get (struct sp_hash *self, uint32_t key);

#endif
