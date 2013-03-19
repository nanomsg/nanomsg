/*
    Copyright (c) 2013 250bpm s.r.o.

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

#ifndef NN_ATOMIC_INCLUDED
#define NN_ATOMIC_INCLUDED


#if 0
AC_MSG_CHECKING([for GCC-style __sync_*() atomic builtins])
AC_LINK_IFELSE([AC_LANG_PROGRAM(
    [volatile void* p;],
    [[  int r;
        r = __sync_bool_compare_and_swap(&p, (void*) 0x12345, (void*) 0);
        return r;]])],
    [gcc_style_sync_atomic=yes],
    [gcc_style_sync_atomic=no])
AC_MSG_RESULT([$gcc_style_sync_atomic])
AS_IF([test "x$gcc_style_sync_atomic" = "xyes"], [
    AC_DEFINE([XS_ATOMIC_GCC_SYNC], [1], [GCC-style __sync_*() atomic builtins])
])
#endif



#if defined NN_HAVE_WINDOWS
#include "win.h"
#define NN_ATOMIC_WINAPI
#elif defined NN_HAVE_GCC_ATOMIC_BUILTINS
#define NN_ATOMIC_GCC_BUILTINS
#else
#include "mutex.h"
#define NN_ATOMIC_MUTEX
#endif

#include <stdint.h>

struct nn_atomic {
#if defined NN_ATOMIC_MUTEX
    struct nn_mutex sync;
#endif
    volatile uint32_t n;
};

/*  Initialise the object. Set it to value 'n'. */
void nn_atomic_init (struct nn_atomic *self, uint32_t n);

/*  Destroy the object. */
void nn_atomic_term (struct nn_atomic *self);

/*  Atomically add n to the object, return old value of the object. */
uint32_t nn_atomic_inc (struct nn_atomic *self, uint32_t n);

/*  Atomically subtract n from the object, return old value of the object. */
uint32_t nn_atomic_dec (struct nn_atomic *self, uint32_t n);

#endif

