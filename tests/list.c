/*
    Copyright (c) 2013 Nir Soffer <nirsof@gmail.com>

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

#include "../src/utils/cont.h"

#include "../src/utils/err.c"
#include "../src/utils/list.c"

static struct nn_list_item sentinel;

struct item {
    int value;
    struct nn_list_item item;
};

static struct item that = {1};
static struct item other = {2};

int main ()
{
    int rc;
    struct nn_list list;
    struct nn_list_item *list_item;
    struct item *item;

    /* Initialize a list. */

    /*  Uninitialized list has random content. */
    list.first = &sentinel;
    list.last = &sentinel;

    nn_list_init (&list);

    nn_assert (list.first == NULL);
    nn_assert (list.last == NULL);
    nn_list_term (&list);
    
    /*  Empty list. */
    
    nn_list_init (&list);

    rc = nn_list_empty (&list);
    nn_assert (rc == 1); 

    list_item = nn_list_begin (&list);
    nn_assert (list_item == NULL);

    list_item = nn_list_end (&list);
    nn_assert (list_item == NULL);

    nn_list_term (&list);

    /*  Insert nil item. */

    nn_list_init (&list);
    nn_list_item_nil (&that.item);

    nn_list_insert (&list, &that.item, nn_list_end (&list));

    nn_assert (that.item.prev == NULL);
    nn_assert (that.item.next == NULL);

    nn_list_clear(&list);
    nn_list_term (&list);

    /*  Insert non-nil item. */

    nn_list_init (&list);

    /*  That is uninitialized, or belong to another list. */
    that.item.prev = &sentinel;
    that.item.next = &sentinel;

    nn_list_insert (&list, &that.item, nn_list_end (&list));

    nn_assert (that.item.prev == NULL);
    nn_assert (that.item.next == NULL);

    nn_list_clear (&list);
    nn_list_term (&list);

    /*  Iterating items. */
    
    nn_list_init (&list);
    nn_list_item_nil (&that.item);

    nn_list_insert (&list, &that.item, nn_list_end (&list));
    
    list_item = nn_list_begin (&list);
    nn_assert (list_item == &that.item);

    item = nn_cont(list_item, struct item, item);
    nn_assert (item == &that);

    list_item = nn_list_end (&list);
    nn_assert (list_item == NULL);

    list_item = nn_list_prev (&list, &that.item);
    nn_assert (list_item == NULL);

    list_item = nn_list_next (&list, &that.item);
    nn_assert (list_item == NULL);

    rc = nn_list_empty (&list);
    nn_assert (rc == 0);

    nn_list_clear (&list);
    nn_list_term (&list);

    /*  Append items. */

    nn_list_init (&list);
    nn_list_item_nil (&that.item);
    nn_list_item_nil (&other.item);

    nn_list_insert (&list, &that.item, nn_list_end (&list));
    nn_list_insert (&list, &other.item, nn_list_end (&list));

    list_item = nn_list_begin (&list);
    nn_assert (list_item == &that.item);

    list_item = nn_list_next (&list, list_item);
    nn_assert (list_item == &other.item);

    nn_list_clear(&list);
    nn_list_term (&list);

    /*  Prepend items. */

    nn_list_init (&list);
    nn_list_item_nil (&that.item);
    nn_list_item_nil (&other.item);

    nn_list_insert (&list, &that.item, nn_list_begin (&list));
    nn_list_insert (&list, &other.item, nn_list_begin (&list));

    list_item = nn_list_begin (&list);
    nn_assert (list_item == &other.item);

    list_item = nn_list_next (&list, list_item);
    nn_assert (list_item == &that.item);

    nn_list_clear(&list);
    nn_list_term (&list);

    /*  Erase item. */

    nn_list_init (&list);
    nn_list_item_nil (&that.item);

    nn_list_insert (&list, &that.item, nn_list_end (&list));

    /*  That belongs to list now. */
    rc = nn_list_item_isnil (&that.item);
    nn_assert (rc == 0);

    list_item = nn_list_begin (&list);
    list_item = nn_list_erase (&list, list_item);
    
    /*  That does not belong to list now. */
    rc = nn_list_item_isnil (&that.item);
    nn_assert (rc == 1);

    nn_assert (list_item == NULL);

    rc = nn_list_empty (&list);
    nn_assert (rc == 1);

    nn_list_term (&list);

    /*  Clear all items. */

    nn_list_init (&list);
    nn_list_item_nil (&that.item);
    nn_list_item_nil (&other.item);

    nn_list_insert (&list, &that.item, nn_list_end (&list));
    nn_list_insert (&list, &other.item, nn_list_end (&list));
    nn_list_clear (&list);

    /*  All items should become nil. */
    rc = nn_list_item_isnil (&that.item);
    nn_assert (rc == 1);
    rc = nn_list_item_isnil (&other.item);
    nn_assert (rc == 1);

    rc = nn_list_empty (&list);
    nn_assert (rc = 1);

    nn_assert (list.first == NULL);
    nn_assert (list.last == NULL);

    nn_list_term (&list);

    return 0;
}

