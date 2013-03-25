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

#include "sem.h"
#include "err.h"
#include "fast.h"

#if defined NN_HAVE_SEMAPHORE

void nn_sem_init (struct nn_sem *self)
{
    int rc;

    rc = sem_init (&self->sem, 0, 0);
    errno_assert (rc == 0);
}

void nn_sem_term (struct nn_sem *self)
{
    int rc;

    rc = sem_destroy (&self->sem);
    errno_assert (rc == 0);
}

void nn_sem_post (struct nn_sem *self)
{
    int rc;

    rc = sem_post (&self->sem);
    errno_assert (rc == 0);
}

int nn_sem_wait (struct nn_sem *self)
{
    int rc;

    rc = sem_wait (&self->sem);
    if (nn_slow (rc < 0 && errno == EINTR))
        return -EINTR;
    errno_assert (rc == 0);
    return 0;
}

#endif

