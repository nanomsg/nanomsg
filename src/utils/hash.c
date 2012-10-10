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

#include "hash.h"
#include "alloc.h"
#include "cont.h"
#include "err.h"

#define SP_HASH_INITIAL_SLOTS 32

static uint32_t sp_hash_key (uint32_t key);

void sp_hash_init (struct sp_hash *self)
{
    int i;

    self->slots = SP_HASH_INITIAL_SLOTS;
    self->items = 0;
    self->array = sp_alloc (sizeof (struct sp_list) * SP_HASH_INITIAL_SLOTS);
    alloc_assert (self->array);
    for (i = 0; i != SP_HASH_INITIAL_SLOTS; ++i)
        sp_list_init (&self->array [i]);
}

void sp_hash_term (struct sp_hash *self)
{
    int i;

    for (i = 0; i != self->slots; ++i)
        sp_list_term (&self->array [i]);
    sp_free (self->array);
}

void sp_hash_insert (struct sp_hash *self, uint32_t key,
    struct sp_hash_item *item)
{
    int slot;
    struct sp_list_item *it;

    slot = sp_hash_key (key) % self->slots;

    for (it = sp_list_begin (&self->array [slot]);
          it != sp_list_end (&self->array [slot]);
          it = sp_list_next (&self->array [slot], it))
        sp_assert (sp_cont (it, struct sp_hash_item, list)->key != key);

    item->key = key;
    sp_list_insert (&self->array [slot], &item->list,
        sp_list_end (&self->array [slot]));
    ++self->items;

    /*  TODO: If the hash is getting full, double the amount of slots and
        re-hash all the items. */
}

void sp_hash_erase (struct sp_hash *self, struct sp_hash_item *item)
{
    int slot;

    slot = sp_hash_key (item->key) % self->slots;
    sp_list_erase (&self->array [slot], &item->list);
}

struct sp_hash_item *sp_hash_get (struct sp_hash *self, uint32_t key)
{
    int slot;
    struct sp_list_item *it;
    struct sp_hash_item *item;

    slot = sp_hash_key (key) % self->slots;

    for (it = sp_list_begin (&self->array [slot]);
          it != sp_list_end (&self->array [slot]);
          it = sp_list_next (&self->array [slot], it)) {
        item = sp_cont (it, struct sp_hash_item, list);
        if (item->key == key)
            return item;
    }

    return NULL;
}

uint32_t sp_hash_key (uint32_t key)
{
    /*  TODO: This is a randomly choosen hashing function. Give some thought
        to picking a more fitting one. */
    key = (key ^ 61) ^ (key >> 16);
    key += key << 3;
    key = key ^ (key >> 4);
    key = key * 0x27d4eb2d;
    key = key ^ (key >> 15);

    return key;
}

