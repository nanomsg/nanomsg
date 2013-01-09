/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#ifndef SP_TRIE_INCLUDED
#define SP_TRIE_INCLUDED

#include <stddef.h>
#include <stdint.h>

/*  This class implements extremely memory-efficient patricia trie. */

/* Maximum length of the prefix. */
#define SP_TRIE_PREFIX_MAX 10

/* Maximum number of children in the sparse mode. */
#define SP_TRIE_SPARSE_MAX 8

/* 'type' is set to this value when in the dense mode. */
#define SP_TRIE_DENSE_TYPE (SP_TRIE_SPARSE_MAX + 1)

/*  This structure represents a node in patricia trie. It's a 32 bytes
    long header to be followed by the array of pointers to child nodes. */
struct sp_trie_node
{
    uint32_t refcount;
    uint8_t prefix_len;
    uint8_t type;
    uint8_t prefix [SP_TRIE_PREFIX_MAX];
    union {
        struct {
            uint8_t children [SP_TRIE_SPARSE_MAX];
        } sparse;
        struct {
            uint8_t min;
            uint8_t max;
            uint16_t nbr;
        } dense;
    };
};
/*  The structure is followed by the array of pointers to children. */

struct sp_trie {

    /*  The root node of the trie (representing the empty subscription). */
    struct sp_trie_node *root;

};

void sp_trie_init (struct sp_trie *self);
void sp_trie_term (struct sp_trie *self);
int sp_trie_subscribe (struct sp_trie *self, const uint8_t *data, size_t size);
int sp_trie_unsubscribe (struct sp_trie *self, const uint8_t *data,
    size_t size);
int sp_trie_match (struct sp_trie *self, const uint8_t *data, size_t size);

/*  Debugging interface. */
void sp_trie_dump (struct sp_trie *self);

#endif

