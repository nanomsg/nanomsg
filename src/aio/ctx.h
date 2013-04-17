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

#ifndef NN_CTX_INCLUDED
#define NN_CTX_INCLUDED

#include "../utils/mutex.h"
#include "../utils/queue.h"

#include "worker.h"
#include "callback.h"
#include "pool.h"

/*  AIO context for objects using AIO subsystem. */

struct nn_ctx_task {
    struct nn_callback *callback;
    void *source;
    int type;
    struct nn_queue_item item;
};

void nn_ctx_task_init (struct nn_ctx_task *self, struct nn_callback *callback,
    void *source, int type);
void nn_ctx_task_term (struct nn_ctx_task *self);

struct nn_ctx {
    struct nn_mutex sync;
    struct nn_pool *pool;
    struct nn_queue tasks;
};

void nn_ctx_init (struct nn_ctx *self, struct nn_pool *pool);
void nn_ctx_term (struct nn_ctx *self);

void nn_ctx_enter (struct nn_ctx *self);
void nn_ctx_leave (struct nn_ctx *self);

struct nn_worker *nn_ctx_choose_worker (struct nn_ctx *self);

void nn_ctx_execute (struct nn_ctx *self, struct nn_ctx_task *task);

#endif

