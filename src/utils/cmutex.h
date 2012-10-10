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

#ifndef SP_CMUTEX_INCLUDED
#define SP_CMUTEX_INCLUDED

#include "mutex.h"

/*  Cancellable mutex. Owner of the critical section may switch into
    'cancellation' mode. There is no change to the behaviour or lock and
    unlock during the cancellation mode. However, cancellable_lock will
    return -ECANCELED while the mutex is in the cancellation mode. */

struct sp_cmutex {
    struct sp_mutex mutex;
    int cancelling;
};

/*  Initialise the mutex. */
void sp_cmutex_init (struct sp_cmutex *self);

/*  Terminate the mutex. */
void sp_cmutex_term (struct sp_cmutex *self);

/*  Lock the mutex. Behaviour of multiple locks from the same thread is
    undefined. */
void sp_cmutex_lock (struct sp_cmutex *self);

/*  Same as lock, however, in cancellation mode function returns -ECANCELED. */
int sp_cmutex_cancellable_lock (struct sp_cmutex *self);

/*  Unlock the mutex. Behaviour of unlocking an unlocked mutex is undefined */
void sp_cmutex_unlock (struct sp_cmutex *self);

/*  Enter the cancellation mode. */
void sp_cmutex_start_cancelling (struct sp_cmutex *self);

/*  Leave the cancellation mode. */
void sp_cmutex_stop_cancelling (struct sp_cmutex *self);

#endif
