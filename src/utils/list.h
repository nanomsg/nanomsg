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

#ifndef NN_LIST_INCLUDED
#define NN_LIST_INCLUDED

struct nn_list_item {
    struct nn_list_item *next;
    struct nn_list_item *prev;
};

struct nn_list {
    struct nn_list_item *first;
    struct nn_list_item *last;
};

/*  Initialise the list. */
void nn_list_init (struct nn_list *self);

/*  Terminate the list. Note that list must be manually emptied before the
    termination. */
void nn_list_term (struct nn_list *self);

/*  Returns 1 is list has zero elements, 0 otherwise. */
int nn_list_empty (struct nn_list *self);

/*  Remove all elements from the list. */
void nn_list_clear (struct nn_list *self);

/*  Returns iterator to the first item in the list. */
struct nn_list_item *nn_list_begin (struct nn_list *self);

/*  Returns iterator to one past the last item in the list. */
struct nn_list_item *nn_list_end (struct nn_list *self);

/*  Returns iterator to an element prior to the one pointed to be it. */
struct nn_list_item *nn_list_prev (struct nn_list *self,
    struct nn_list_item *it);

/*  Returns iterator to one past the element pointed to be it. */
struct nn_list_item *nn_list_next (struct nn_list *self,
    struct nn_list_item *it);

/*  Adds the item to the list before the item pointed to by 'it'. */
void nn_list_insert (struct nn_list *self, struct nn_list_item *item,
    struct nn_list_item *it);

/*  Removes the element from the list it is in at the moment. Returns pointer
    to the next element in the list. */
struct nn_list_item *nn_list_erase (struct nn_list *self,
    struct nn_list_item *item);

/*  Normally, when item is not part of a list, its value is undefined. This
    function sets it to nil value. */
void nn_list_item_nil (struct nn_list_item *self);

/*  Returns 1 is the item was set to nil, or 0 if it is part of a list. */
int nn_list_item_isnil (struct nn_list_item *self);

#endif
