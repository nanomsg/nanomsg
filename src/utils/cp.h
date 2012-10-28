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

#ifndef SP_CP_INCLUDED
#define SP_CP_INCLUDED

#include "mutex.h"
#include "cond.h"

#if defined SP_HAVE_WINDOWS
#include "win.h"
#endif

#include <stddef.h>

struct sp_usock;

/*  Implementation of platform-neutral completion port. */

struct sp_cp {
#if defined SP_HAVE_WINDOWS
    HANDLE hndl;
#else
    struct sp_mutex sync;
    struct sp_cond cond;
    size_t capacity;
    size_t head;
    size_t tail;
    struct sp_cp_item {
        int op;
        void *arg;
    } *items;
#endif
};

void sp_cp_init (struct sp_cp *self);
void sp_cp_term (struct sp_cp *self);
void sp_cp_post (struct sp_cp *self, int op, void *arg);

/*  The function is suspectible to spurious ETIMEDOUT wake-ups. */
int sp_cp_wait (struct sp_cp *self, int timeout, int *op, void **arg);

/*  Associates the socket with the completion point. Association is broken
    when the socket is destroyed. */
void sp_cp_register_usock (struct sp_cp *self, struct sp_usock *usock);

#endif

