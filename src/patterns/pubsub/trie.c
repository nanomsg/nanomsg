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

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "trie.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

/*  Forward declarations. */
static struct sp_trie_node *sp_node_compact (struct sp_trie_node *self);
static int sp_node_check_prefix (struct sp_trie_node *self,
    const uint8_t *data, size_t size);
static struct sp_trie_node **sp_node_child (struct sp_trie_node *self,
    int index);
static struct sp_trie_node **sp_node_next (struct sp_trie_node *self,
    uint8_t c);
static int sp_node_unsubscribe (struct sp_trie_node **self,
    const uint8_t *data, size_t size);
static void sp_node_term (struct sp_trie_node *self);
static int sp_node_has_subscribers (struct sp_trie_node *self);
static void sp_node_dump (struct sp_trie_node *self, int indent);
static void sp_node_ident (int indent);

void sp_trie_init (struct sp_trie *self)
{
    self->root = NULL;
}

void sp_trie_term (struct sp_trie *self)
{
    sp_node_term (self->root);
}

void sp_trie_dump (struct sp_trie *self)
{
    sp_node_dump (self->root, 0);
}

void sp_node_dump (struct sp_trie_node *self, int indent)
{
    int i;
    int children;

    if (!self) {
        sp_node_ident (indent);
        printf ("NULL\n");
        return;
    }

    sp_node_ident (indent);
    printf ("===================\n");
    sp_node_ident (indent);
    printf ("prefix_len=%d\n", (int) self->prefix_len);
    sp_node_ident (indent);
    printf ("type=%d\n", (int) self->type);
    sp_node_ident (indent);
    printf ("prefix=\"");
    for (i = 0; i != self->prefix_len; ++i)
        putchar (self->prefix [i]);
    printf ("\"\n");
    if (self->type <= 8) {
        sp_node_ident (indent);
        printf ("sparse.children=\"");
        for (i = 0; i != self->type; ++i)
            putchar (self->sparse.children [i]);
        printf ("\"\n");
        children = self->type;
    }
    else {
        sp_node_ident (indent);
        printf ("dense.min='%c' (%d)\n", (char) self->dense.min,
            (int) self->dense.min);
        sp_node_ident (indent);
        printf ("dense.max='%c' (%d)\n", (char) self->dense.max,
            (int) self->dense.max);
        sp_node_ident (indent);
        printf ("dense.nbr=%d\n", (int) self->dense.nbr);
        children = self->dense.max - self->dense.min + 1;
    }

    for (i = 0; i != children; ++i)
        sp_node_dump (((struct sp_trie_node**) (self + 1)) [i], indent + 1);

    sp_node_ident (indent);
    printf ("===================\n");
}

void sp_node_ident (int indent)
{
    int i;

    for (i = 0; i != indent * 4; ++i)
        putchar (' ');
}

void sp_node_term (struct sp_trie_node *self)
{
    int children;
    int i;

    /*  Trivial case of the recursive algorithm. */
    if (!self)
        return;

    /*  Recursively destroy the child nodes. */
    children = self->type <= SP_TRIE_SPARSE_MAX ?
        self->type : (self->dense.max - self->dense.min + 1);
    for (i = 0; i != children; ++i)
        sp_node_term (*sp_node_child (self, i));

    /*  Deallocate this node. */
    sp_free (self);
}

int sp_node_check_prefix (struct sp_trie_node *self,
    const uint8_t *data, size_t size)
{
    /*  Check how many charecters from the data match the prefix. */

    int i;

    for (i = 0; i != self->prefix_len; ++i) {
        if (!size || self->prefix [i] != *data)
            return i;
        ++data;
        --size;
    }
    return self->prefix_len;
}

struct sp_trie_node **sp_node_child (struct sp_trie_node *self, int index)
{
    /*  Finds pointer to the n-th child of the node. */

    return ((struct sp_trie_node**) (self + 1)) + index;
}

struct sp_trie_node **sp_node_next (struct sp_trie_node *self, uint8_t c)
{
    /*  Finds the pointer to the next node based on the supplied character.
      If there is no such pointer, it returns NULL. */

    int i;

    if (self->type == 0)
        return NULL;

    /*  Sparse mode. */
    if (self->type <= 8) {
        for (i = 0; i != self->type; ++i)
            if (self->sparse.children [i] == c)
                return sp_node_child (self, i);
        return NULL;
    }

    /*  Dense mode. */
    if (c < self->dense.min || c > self->dense.max)
        return NULL;
    return sp_node_child (self, c - self->dense.min);
}

struct sp_trie_node *sp_node_compact (struct sp_trie_node *self)
{
    /*  Tries to merge the node with the child node. Returns pointer to
        the comapcted node. */

    struct sp_trie_node *ch;

    /*  Node that is a subscription cannot be compacted. */
    if (sp_node_has_subscribers (self))
        return self;

    /*  Only a node with a single child can be compacted. */
    if (self->type != 1)
        return self;

    /*  Check whether combined prefixes would fix into a single node. */
    ch = *sp_node_child (self, 0);
    if (self->prefix_len + ch->prefix_len + 1 > SP_TRIE_PREFIX_MAX)
        return self;

    /*  Concatenate the prefixes. */
    memmove (ch->prefix + self->prefix_len + 1, ch->prefix, ch->prefix_len);
    memcpy (ch->prefix, self->prefix, self->prefix_len);
    ch->prefix [self->prefix_len] = self->sparse.children [0];
    ch->prefix_len += self->prefix_len + 1;

    /*  Get rid of the obsolete parent node. */
    sp_free (self);

    /*  Return the new compacted node. */
    return ch;
}

int sp_trie_subscribe (struct sp_trie *self, const uint8_t *data, size_t size)
{
    int i;
    struct sp_trie_node **node;
    struct sp_trie_node **n;
    struct sp_trie_node *ch;
    struct sp_trie_node *old_node;
    int pos;
    uint8_t c;
    uint8_t c2;
    uint8_t new_min;
    uint8_t new_max;
    int old_children;
    int new_children;
    int inserted;
    int more_nodes;

    /*  Step 1 -- Traverse the trie. */

    node = &self->root;
    pos = 0;
    while (1) {

        /*  If there are no more nodes on the path, go to step 4. */
        if (!*node)
            goto step4;

        /*  Check whether prefix matches the new subscription. */
        pos = sp_node_check_prefix (*node, data, size);
        data += pos;
        size -= pos;

        /*  If only part of the prefix matches, go to step 2. */
        if (pos < (*node)->prefix_len)
            goto step2;

        /*  Even if whole prefix matches and there's no more data to match,
            go directly to step 5. */
        if (!size)
            goto step5;

        /*  Move to the next node. If it is not present, go to step 3. */
        n = sp_node_next (*node, *data);
        if (!n)
            goto step3;
        node = n;
        ++data;
        --size;
    }

    /*  Step 2 -- Split the prefix into two parts if required. */
step2:

    ch = *node;
    *node = sp_alloc (sizeof (struct sp_trie_node) +
        sizeof (struct sp_trie_node*));
    assert (*node);
    (*node)->prefix_len = pos;
    (*node)->type = 1;
    memcpy ((*node)->prefix, ch->prefix, pos);
    (*node)->sparse.children [0] = ch->prefix [pos];
    ch->prefix_len -= (pos + 1);
    memmove (ch->prefix, ch->prefix + pos + 1, ch->prefix_len);
    ch = sp_node_compact (ch);
    *sp_node_child (*node, 0) = ch;
    pos = (*node)->prefix_len;

    /*  Step 3 -- Adjust the child array to accommodate the new character. */
step3:

    /*  If there are no more data in the subscription, there's nothing to
        adjust in the child array. Proceed directly to the step 5. */
    if (!size)
        goto step5;

    /*  If the new branch fits into sparse array... */
    if ((*node)->type < SP_TRIE_SPARSE_MAX) {
        *node = sp_realloc (*node, sizeof (struct sp_trie_node) +
            ((*node)->type + 1) * sizeof (struct sp_trie_node*));
        assert (*node);
        (*node)->sparse.children [(*node)->type] = *data;
        ++(*node)->type;
        node = sp_node_child (*node, (*node)->type - 1);
        *node = NULL;
        ++data;
        --size;
        goto step4;
    }

    /*  If the node is already a dense array, resize it to fit the next
        character. */
    if ((*node)->type == SP_TRIE_DENSE_TYPE) {
        c = *data;
        if (c < (*node)->dense.min || c > (*node)->dense.max) {
            new_min = (*node)->dense.min < c ? (*node)->dense.min : c;
            new_max = (*node)->dense.max > c ? (*node)->dense.max : c;
            *node = sp_realloc (*node, sizeof (struct sp_trie_node) +
                (new_max - new_min + 1) * sizeof (struct sp_trie_node*));
            assert (*node);
            old_children = (*node)->dense.max - (*node)->dense.min + 1;
            new_children = new_max - new_min + 1;
            if ((*node)->dense.min != new_min) {
                inserted = (*node)->dense.min - new_min;
                memmove (sp_node_child (*node, inserted),
                    sp_node_child (*node, 0),
                    old_children * sizeof (struct sp_trie_node*));
                memset (sp_node_child (*node, 0), 0,
                    inserted * sizeof (struct sp_trie_node*));
            }
            else {
                memset (sp_node_child (*node, old_children), 0,
                    (new_children - old_children) *
                    sizeof (struct sp_trie_node*));
            }
            (*node)->dense.min = new_min;
            (*node)->dense.max = new_max;
            ++(*node)->dense.nbr;
        }
        node = sp_node_child (*node, c - (*node)->dense.min);
        ++data;
        --size;
        goto step4;
    }

    /*  This is a sparse array, but no more children can be added to it.
        We have to convert it into a dense array. */
    {
        /*  First, determine the range of children. */
        new_min = 255;
        new_max = 0;
        for (i = 0; i != (*node)->type; ++i) {
            c2 = (*node)->sparse.children [i];
            new_min = new_min < c2 ? new_min : c2;
            new_max = new_max > c2 ? new_max : c2;
        }
        new_min = new_min < *data ? new_min : *data;
        new_max = new_max > *data ? new_max : *data;

        /*  Create a new mode, while keeping the old one for a while. */
        old_node = *node;
        *node = (struct sp_trie_node*) sp_alloc (sizeof (struct sp_trie_node) +
            (new_max - new_min + 1) * sizeof (struct sp_trie_node*));
        assert (*node);

        /*  Fill in the new node. */
        (*node)->prefix_len = old_node->prefix_len;
        (*node)->type = SP_TRIE_DENSE_TYPE;
        memcpy ((*node)->prefix, old_node->prefix, old_node->prefix_len);
        (*node)->dense.min = new_min;
        (*node)->dense.max = new_max;
        (*node)->dense.nbr = old_node->type + 1;
        memset (*node + 1, 0, (new_max - new_min + 1) *
            sizeof (struct sp_trie_node*));
        for (i = 0; i != old_node->type; ++i)
            *sp_node_child (*node, old_node->sparse.children [i] - new_min) =
                *sp_node_child (old_node, i);
        node = sp_node_next (*node, *data);
        ++data;
        --size;

        /*  Get rid of the obsolete old node. */
        sp_free (old_node);
    }

    /*  Step 4 -- Create new nodes for remaining part of the subscription. */
step4:

    assert (!*node);
    while (1) {

        /*  Create a new node to hold the next part of the subscription. */
        more_nodes = size > SP_TRIE_PREFIX_MAX;
        *node = sp_alloc (sizeof (struct sp_trie_node) +
            (more_nodes ? sizeof (struct sp_trie_node*) : 0));
        assert (*node);

        /*  Fill in the new node. */
        (*node)->type = more_nodes ? 1 : 0;
        (*node)->prefix_len = size < (size_t) SP_TRIE_PREFIX_MAX ?
            size : (size_t) SP_TRIE_PREFIX_MAX;
        memcpy ((*node)->prefix, data, (*node)->prefix_len);
        data += (*node)->prefix_len;
        size -= (*node)->prefix_len;
        if (!more_nodes)
            break;
        (*node)->sparse.children [0] = *data;
        node = sp_node_child (*node, 0);
        ++data;
        --size;
    }

    /*  Step 5 -- Create the subscription as such. */
step5:

    ++(*node)->refcount;

    /*  Return 1 in case of a fresh subscription. */
    return (*node)->refcount == 1 ? 1 : 0;
}

int sp_trie_match (struct sp_trie *self, const uint8_t *data, size_t size)
{
    struct sp_trie_node *node;

    node = self->root;
    while (1) {

        /*  If there's no more data to match, return. */
        if (!size)
            return sp_node_has_subscribers (node) ? 1 : 0;

        /*  If we are at the end of the trie, return. */
        if (!node)
            return 0;

        /*  If prefix does not match the data, return. */
        if (sp_node_check_prefix (node, data, size) != node->prefix_len)
            return 0;

        /*  Skip the prefix. */
        data += node->prefix_len;
        size -= node->prefix_len;

        /*  If there's at least one subscriber, the message is matching. */
        if (sp_node_has_subscribers (node))
            return 1;

        /*  If there's no more data to match, return. */
        if (!size)
            return 0;

        /*  Move to the next node. */
        node = *sp_node_next (node, *data);
        ++data;
        --size;
    }
}

int sp_trie_unsubscribe (struct sp_trie *self, const uint8_t *data, size_t size)
{
    return sp_node_unsubscribe (&self->root, data, size);
}

static int sp_node_unsubscribe (struct sp_trie_node **self,
    const uint8_t *data, size_t size)
{
    int i;
    int j;
    int index;
    int ptr_index;
    int new_min;
    struct sp_trie_node **ch;
    struct sp_trie_node *new_node;
    struct sp_trie_node *ch2;

    if (!size)
        goto found;

    /*  If prefix does not match the data, return. */
    if (sp_node_check_prefix (*self, data, size) != (*self)->prefix_len)
        return 0;

    /*  Skip the prefix. */
    data += (*self)->prefix_len;
    size -= (*self)->prefix_len;

    if (!size)
        goto found;

    /*  Move to the next node. */
    ch = sp_node_next (*self, *data);
    if (!ch)
        return 0; /*  TODO: This should be an error. */

    /*  Recursive traversal of the trie happens here. If the subscription
        wasn't really removed, nothing have changed in the trie and
        no additional pruning is needed. */
    if (sp_node_unsubscribe (ch, data + 1, size - 1) == 0)
        return 0;

    /*  Subscription removal is already done. Now we are going to compact
        the trie. However, if the following node remains in place, there's
        nothing to compact here. */
    if (*ch)
        return 1;

    /*  Sparse array. */
    if ((*self)->type < SP_TRIE_DENSE_TYPE) {

        /*  Get the indices of the removed child. */
        for (index = 0; index != (*self)->type; ++index)
            if ((*self)->sparse.children [index] == *data)
                break;
        assert (index != (*self)->type);
        ptr_index = (*self)->sparse.children [index];

        /*  Remove the destroyed child from both lists of children. */
        memmove (
            (*self)->sparse.children + index,
            (*self)->sparse.children + index + 1,
            (*self)->type - index - 1);
        memmove (
            sp_node_child (*self, ptr_index),
            sp_node_child (*self, ptr_index + 1),
            ((*self)->type - index - 1) * sizeof (struct sp_trie_node*));
        --(*self)->type;
        for (i = 0; i != (*self)->type; ++i)
            if ((*self)->sparse.children [i] >= ptr_index)
                --((*self)->sparse.children [i]);
        *self = sp_realloc (*self, sizeof (struct sp_trie_node) +
            ((*self)->type * sizeof (struct sp_trie_node*)));
        assert (*self);
        
        /*  If there are no more children and no refcount, we can delete
            the node altogether. */
        if (!(*self)->type && !sp_node_has_subscribers (*self)) {
            sp_free (*self);
            *self = NULL;
            return 1;
        }

        /*  Try to merge the node with the following node. */
        *self = sp_node_compact (*self);

        return 1;
    }

    /*  Dense array. */

    /*  In this case the array stays dense. We have to adjust the limits of
        the array, if appropriate. */
    if ((*self)->dense.nbr > SP_TRIE_SPARSE_MAX + 1) {

        /*  If the removed item is the leftmost one, trim the array from
            the left side. */
        if (*data == (*self)->dense.min) {
             for (i = 0; i != (*self)->dense.max - (*self)->dense.min + 1;
                   ++i)
                 if (*sp_node_child (*self, i))
                     break;
             new_min = i + (*self)->dense.min;
             memmove (sp_node_child (*self, 0), sp_node_child (*self, i),
                 ((*self)->dense.max - new_min + 1) *
                 sizeof (struct sp_trie_node*));
             (*self)->dense.min = new_min;
             --(*self)->dense.nbr;
             *self = sp_realloc (*self, sizeof (struct sp_trie_node) +
                 ((*self)->dense.max - new_min + 1) *
                 sizeof (struct sp_trie_node*));
             assert (*self);
             return 1;
        }

        /*  If the removed item is the rightmost one, trim the array from
            the right side. */
        if (*data == (*self)->dense.max) {
             for (i = (*self)->dense.max - (*self)->dense.min; i != 0; --i)
                 if (*sp_node_child (*self, i))
                     break;
             (*self)->dense.max = i + (*self)->dense.min;
             --(*self)->dense.nbr;
             *self = sp_realloc (*self, sizeof (struct sp_trie_node) +
                 ((*self)->dense.max - (*self)->dense.min + 1) *
                 sizeof (struct sp_trie_node*));
             assert (*self);
             return 1;
        }

        /*  If the item is removed from the middle of the array, do nothing. */
        --(*self)->dense.nbr;
        return 1;
    }

    /*  Convert dense array into sparse array. */
    {
        new_node = sp_alloc (sizeof (struct sp_trie_node) +
            SP_TRIE_SPARSE_MAX * sizeof (struct sp_trie_node*));
        assert (new_node);
        new_node->prefix_len = (*self)->prefix_len;
        memcpy (new_node->prefix, (*self)->prefix, new_node->prefix_len);
        new_node->type = SP_TRIE_SPARSE_MAX;
        j = 0;
        for (i = 0; i != (*self)->dense.max - (*self)->dense.min + 1;
              ++i) {
            ch2 = *sp_node_child (*self, i);
            if (ch2) {
                new_node->sparse.children [j] = i + (*self)->dense.min;
                *sp_node_child (new_node, j) = ch2;
                ++j;
            }
        }
        assert (j == SP_TRIE_SPARSE_MAX);
        sp_free (*self);
        *self = new_node;
        return 1;
    }

found:

    /*  We are at the end of the subscription here. */

    /*  Subscription doesn't exist. */
    if (sp_slow (!*self || !sp_node_has_subscribers (*self)))
        return -EINVAL;

    /*  Subscription exists. Unsubscribe. */
    --(*self)->refcount;

    /*  If reference count has dropped to zero we can try to compact
        the node. */
    if (!(*self)->refcount) {

        /*  If there are no children, we can delete the node altogether. */
        if (!(*self)->type) {
            sp_free (*self);
            *self = NULL;
            return 1;
        }

        /*  Try to merge the node with the following node. */
        *self = sp_node_compact (*self);
        return 1;
    }

    return 0;
}

int sp_node_has_subscribers (struct sp_trie_node *node)
{
    //  Returns 1 when there are no subscribers associated with the node.
    return node->refcount ? 1 : 0;
}

