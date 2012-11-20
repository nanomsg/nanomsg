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

#include <stddef.h>

#include "queue.h"


void sp_queue_init (struct sp_queue *self)
{
    self->head = NULL;
    self->tail = NULL;
}

void sp_queue_term (struct sp_queue *self)
{
    self->head = NULL;
    self->tail = NULL;
}

void sp_queue_push (struct sp_queue *self, struct sp_queue_item *item)
{
    item->next = NULL;
    if (!self->head)
        self->head = item;
    if (self->tail)
        self->tail->next = item;
    self->tail = item;
}

struct sp_queue_item *sp_queue_pop (struct sp_queue *self)
{
    struct sp_queue_item *result;

    if (!self->head)
        return NULL;
    result = self->head;
    self->head = result->next;
    if (!self->head)
        self->tail = NULL;
    return result;
}

