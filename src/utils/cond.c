/*
    Copyright (c) 2016 Franklin "Snaipe" Mathieu <franklinmathieu@gmail.com>

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

#include "cond.h"
#include "err.h"

#ifdef NN_HAVE_WINDOWS
/*  Not implemented (no current usage in the library on windows)  */

void nn_cond_init (struct nn_cond *self)
{
    nn_assert (0);
}

void nn_cond_term (struct nn_cond *self)
{
    nn_assert (0);
}

void nn_cond_wait (struct nn_cond *self, struct nn_mutex *mut)
{
    nn_assert (0);
}

void nn_cond_signal (struct nn_cond *self)
{
    nn_assert (0);
}
#else

void nn_cond_init (struct nn_cond *self)
{
    int rc;

    rc = pthread_cond_init (&self->cond, NULL);
    errnum_assert (rc == 0, rc);
}

void nn_cond_term (struct nn_cond *self)
{
    int rc;

    rc = pthread_cond_destroy (&self->cond);
    errnum_assert (rc == 0, rc);
}

void nn_cond_wait (struct nn_cond *self, struct nn_mutex *mut)
{
    int rc;

    rc = pthread_cond_wait (&self->cond, &mut->mutex);
    errnum_assert (rc == 0, rc);
}

void nn_cond_signal (struct nn_cond *self)
{
    int rc;

    rc = pthread_cond_broadcast (&self->cond);
    errnum_assert (rc == 0, rc);
}

#endif
