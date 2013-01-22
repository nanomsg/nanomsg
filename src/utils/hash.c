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
#include "fast.h"
#include "alloc.h"
#include "cont.h"
#include "err.h"

#define NN_HASH_INITIAL_SLOTS 32

static uint32_t nn_hash_key (uint32_t key);

void nn_hash_init (struct nn_hash *self)
{
    int i;

    self->slots = NN_HASH_INITIAL_SLOTS;
    self->items = 0;
    self->array = nn_alloc (sizeof (struct nn_list) * NN_HASH_INITIAL_SLOTS,
        "hash map");
    alloc_assert (self->array);
    for (i = 0; i != NN_HASH_INITIAL_SLOTS; ++i)
        nn_list_init (&self->array [i]);
}

void nn_hash_term (struct nn_hash *self)
{
    int i;

    for (i = 0; i != self->slots; ++i)
        nn_list_term (&self->array [i]);
    nn_free (self->array);
}

void nn_hash_insert (struct nn_hash *self, uint32_t key,
    struct nn_hash_item *item)
{
    int slot;
    int newslot;
    struct nn_list_item *it;
    struct nn_list_item *newit;
    struct nn_list *oldarray;
    struct nn_hash_item *itm;

    slot = nn_hash_key (key) % self->slots;

    for (it = nn_list_begin (&self->array [slot]);
          it != nn_list_end (&self->array [slot]);
          it = nn_list_next (&self->array [slot], it))
        nn_assert (nn_cont (it, struct nn_hash_item, list)->key != key);

    item->key = key;
    nn_list_insert (&self->array [slot], &item->list,
        nn_list_end (&self->array [slot]));
    ++self->items;

    /*  If the hash is getting full, double the amount of slots and
        re-hash all the items. */
    if (nn_slow (self->items * 2 > self->slots)) {
        oldarray = self->array;
        self->slots *= 2;
        self->array = nn_alloc (sizeof (struct nn_list) * self->slots,
            "hash map");
        alloc_assert (self->array);
        for (slot = 0; slot != self->slots; ++slot) {
            for (it = nn_list_begin (&oldarray [slot]);
                  it != nn_list_end (&oldarray [slot]);
                  it = nn_list_next (&oldarray [slot], it)) {
                nn_list_erase (&self->array [slot], it);
                itm = nn_cont (it, struct nn_hash_item, list);
                newslot = nn_hash_key (itm->key) % self->slots;
                for (newit = nn_list_begin (&self->array [newslot]);
                      newit != nn_list_end (&self->array [newslot]);
                      newit = nn_list_next (&self->array [newslot], newit))
                    nn_assert (nn_cont (newit, struct nn_hash_item,
                        list)->key != itm->key);
                nn_list_insert (&self->array [newslot], &itm->list,
                        nn_list_end (&self->array [newslot]));
            }
            nn_list_term (&self->array [slot]);
        }
        nn_free (oldarray);
    }
}

void nn_hash_erase (struct nn_hash *self, struct nn_hash_item *item)
{
    int slot;

    slot = nn_hash_key (item->key) % self->slots;
    nn_list_erase (&self->array [slot], &item->list);
}

struct nn_hash_item *nn_hash_get (struct nn_hash *self, uint32_t key)
{
    int slot;
    struct nn_list_item *it;
    struct nn_hash_item *item;

    slot = nn_hash_key (key) % self->slots;

    for (it = nn_list_begin (&self->array [slot]);
          it != nn_list_end (&self->array [slot]);
          it = nn_list_next (&self->array [slot], it)) {
        item = nn_cont (it, struct nn_hash_item, list);
        if (item->key == key)
            return item;
    }

    return NULL;
}

uint32_t nn_hash_key (uint32_t key)
{
    /*  TODO: This is a randomly chosen hashing function. Give some thought
        to picking a more fitting one. */
    key = (key ^ 61) ^ (key >> 16);
    key += key << 3;
    key = key ^ (key >> 4);
    key = key * 0x27d4eb2d;
    key = key ^ (key >> 15);

    return key;
}

