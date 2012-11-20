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

#ifndef SP_LIST_INCLUDED
#define SP_LIST_INCLUDED

struct sp_list_item {
    struct sp_list_item *next;
    struct sp_list_item *prev;
};

struct sp_list {
    struct sp_list_item *first;
    struct sp_list_item *last;
};

/*  Initialise the list. */
void sp_list_init (struct sp_list *self);

/*  Terminate the list. Note that list must be manually emptied before the
    termination. */
void sp_list_term (struct sp_list *self);

/*  Returns 1 is list has zero elements, 0 otherwise. */
int sp_list_empty (struct sp_list *self);

/*  Returns iterator to the first item in the list. */
struct sp_list_item *sp_list_begin (struct sp_list *self);

/*  Rreturns iterator to one past the last item in the list. */
struct sp_list_item *sp_list_end (struct sp_list *self);

/*  Returns iterator to an element prior to the one pointed to be it. */
struct sp_list_item *sp_list_prev (struct sp_list *self,
    struct sp_list_item *it);

/*  Returns iterator to one past the element pointed to be it. */
struct sp_list_item *sp_list_next (struct sp_list *self,
    struct sp_list_item *it);

/*  Adds the item to the list before the item pointed to by 'it'. */
void sp_list_insert (struct sp_list *self, struct sp_list_item *item,
    struct sp_list_item *it);

/*  Removes the element from the list it is in at the moment. Returns pointer
    to the next element in the list. */
struct sp_list_item *sp_list_erase (struct sp_list *self,
    struct sp_list_item *item);

#endif
