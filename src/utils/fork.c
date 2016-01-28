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

#include <pthread.h>
#include <errno.h>
#include "fork.h"
#include "glock.h"
#include "../core/global.h"
#include "../aio/pool.h"

#ifdef NN_RESET_AFTER_FORK
/* Reset strategy: We close all sockets in the child on fork */

static void nn_prefork (void)
{
    /* Retrieve the worker and lock it */
    struct nn_pool *pool = nn_global_getpool();
    struct nn_worker *w = &pool->worker;

    nn_worker_pause (w);

    nn_glock_lock ();
    nn_global_lock_all_sockets ();
    nn_mutex_lock (&w->sync);
}

static void nn_postfork_parent (void)
{
    /* Unlock the worker */
    struct nn_pool *pool = nn_global_getpool();
    struct nn_worker *w = &pool->worker;

    nn_mutex_unlock (&w->sync);
    nn_global_unlock_all_sockets ();
    nn_glock_unlock ();

    nn_worker_resume (w);
}

static void nn_postfork_child (void)
{
    /* Unlock the worker and reset */
    struct nn_pool *pool = nn_global_getpool();
    struct nn_worker *w = &pool->worker;

    /* Don't try to resume the dead thread, but simply release the lock */
    nn_mutex_unlock (&w->resume_mutex);

    /* Revive the dead worker */
    nn_worker_revive (w);

    nn_mutex_unlock (&w->sync);
    nn_global_unlock_all_sockets ();

    nn_global_postfork_cleanup ();
    nn_glock_unlock ();
}
#else
/*  No strategy: do nothing. */

static void nn_prefork (void)
{
}

static void nn_postfork_child (void)
{
}

static void nn_postfork_parent (void)
{
}
#endif

/* Set up atfork handlers */

int nn_setup_atfork_handlers (void)
{
    int res = pthread_atfork (nn_prefork, nn_postfork_parent, nn_postfork_child);
    if (res != 0) {
        errno = res;
        return -1;
    }
    return 0;
}
