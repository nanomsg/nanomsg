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

#include "cmutex.h"
#include "fast.h"
#include "err.h"

void sp_cmutex_init (struct sp_cmutex *self)
{
    sp_mutex_init (&self->mutex, 0);
    self->cancelling = 0;
}

void sp_cmutex_term (struct sp_cmutex *self)
{
    sp_mutex_term (&self->mutex);
}

void sp_cmutex_lock (struct sp_cmutex *self)
{
    while (1) {
        sp_mutex_lock (&self->mutex);
        if (sp_fast (!self->cancelling))
            return;
        sp_mutex_unlock (&self->mutex);

        /*  Busy-loop while in cancellation mode. */
    }
}


int sp_cmutex_cancellable_lock (struct sp_cmutex *self)
{
    sp_mutex_lock (&self->mutex);
    if (sp_fast (!self->cancelling))
        return 0;
    sp_mutex_unlock (&self->mutex);
    return -ECANCELED;
}

void sp_cmutex_unlock (struct sp_cmutex *self)
{
    sp_mutex_unlock (&self->mutex);
}

void sp_cmutex_start_cancelling (struct sp_cmutex *self)
{
    self->cancelling = 1;
    sp_mutex_unlock (&self->mutex);
}

void sp_cmutex_stop_cancelling (struct sp_cmutex *self)
{
    sp_mutex_lock (&self->mutex);
    self->cancelling = 0;
}

