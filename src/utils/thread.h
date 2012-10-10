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

#ifndef SP_THREAD_INCLUDED
#define SP_THREAD_INCLUDED

/*  Platform independent implementation of threading. */

#include "config.h"
#ifdef SP_HAVE_WINDOWS
#include "win.h"
#else
#include <pthread.h>
#endif

typedef void (sp_thread_routine) (void*);

struct sp_thread
{
    sp_thread_routine *routine;
    void *arg;
#ifdef SP_HAVE_WINDOWS
    HANDLE handle;
#else
    pthread_t handle;
#endif
};

void sp_thread_init (struct sp_thread *self,
    sp_thread_routine *routine, void *arg);
void sp_thread_term (struct sp_thread *self);

#endif
