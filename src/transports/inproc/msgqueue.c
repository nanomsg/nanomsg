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

#include "msgqueue.h"

#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

#include <string.h>

void sp_msgqueue_init (struct sp_msgqueue *self, size_t maxmem)
{
    struct sp_msgqueue_chunk *chunk;

    sp_mutex_init (&self->sync);

    self->count = 0;
    self->mem = 0;
    self->maxmem = maxmem;

    chunk = sp_alloc (sizeof (struct sp_msgqueue_chunk), "msgqueue chunk");
    alloc_assert (chunk);
    chunk->next = NULL;

    self->out.chunk = chunk;
    self->out.pos = 0;
    self->in.chunk = chunk;
    self->in.pos = 0;

    self->cache = NULL;
}

void sp_msgqueue_term (struct sp_msgqueue *self)
{
    int rc;
    size_t sz;

    /*  Deallocate messages in the pipe. */
    while (1) {
        sz = 0;
        rc = sp_msgqueue_recv (self, NULL, &sz);
        if (rc == -EAGAIN)
            break;
    }

    /*  There are no more messages in the pipe so there's at most one chunk
        in the queue. Deallocate it. */
    sp_assert (self->in.chunk == self->out.chunk);
    sp_free (self->in.chunk);

    /*  Deallocate the cached chunk, if any. */
    if (self->cache)
        sp_free (self->cache);

    sp_mutex_term (&self->sync);
}

int sp_msgqueue_send (struct sp_msgqueue *self, const void *buf, size_t len)
{
    int result;
    struct sp_msg *msg;

    sp_mutex_lock (&self->sync);

    /*  If the message doesn't fit into the queue return error. Note that
        message of any size can be written to an empty queue. This way even
        the messages larger than maximal queue size can be transferred. */
    if (sp_slow (self->count && self->mem + len > self->maxmem)) {
        sp_mutex_unlock (&self->sync);
        return -EAGAIN;
    }

    /*  Adjust the statistics. */
    result = self->count ? 0 : SP_MSGQUEUE_SIGNAL;
    ++self->count;
    self->mem += len;
    if (self->mem >= self->maxmem)
        result |= SP_MSGQUEUE_RELEASE;

    /*  Move the content of the message to the pipe. */
    msg = &self->out.chunk->msgs [self->out.pos];
    sp_msg_init (msg, len);
    if (len)
        memcpy (sp_msg_data (msg), buf, len);
    ++self->out.pos;

    /*  If there's no space for a new message in the pipe, either re-use
        the cache chunk or allocate a new chunk if it does not exist. */
    if (sp_slow (self->out.pos == SP_MSGQUEUE_GRANULARITY)) {
        if (sp_slow (!self->cache)) {
            self->cache = sp_alloc (sizeof (struct sp_msgqueue_chunk),
                "msgqueue chunk");
            alloc_assert (self->cache);
            self->cache->next = NULL;
        }
        self->out.chunk->next = self->cache;
        self->out.chunk = self->cache;
        self->cache = NULL;
        self->out.pos = 0;
    }

    sp_mutex_unlock (&self->sync);

    return result;
}

int sp_msgqueue_recv (struct sp_msgqueue *self, void *buf, size_t *len)
{
    int result;
    struct sp_msg *msg;
    size_t msgsz;
    size_t to_copy;
    struct sp_msgqueue_chunk *o;

    sp_mutex_lock (&self->sync);

    /*  If there is no message in the queue. */
    if (sp_slow (!self->count)) {
        sp_mutex_unlock (&self->sync);
        return -EAGAIN;
    }

    /*  Move the message from the pipe to the user. */
    msg = &self->in.chunk->msgs [self->in.pos];
    msgsz = sp_msg_size (msg);
    to_copy = msgsz < *len ? msgsz : *len;
    if (to_copy)
        memcpy (buf, sp_msg_data (msg), to_copy);
    sp_msg_term (msg);

    /*  Move to the next position. */
    ++self->in.pos;
    if (sp_slow (self->in.pos == SP_MSGQUEUE_GRANULARITY)) {
        o = self->in.chunk;
        self->in.chunk = self->in.chunk->next;
        self->in.pos = 0;
        if (sp_fast (!self->cache))
            self->cache = o;
        else
            sp_free (o);
    }

    /*  Adjust the statistics. */
    /*  TODO: Implement a real queue size limit instead of this fake one. */
    result = self->mem >= self->maxmem ? SP_MSGQUEUE_SIGNAL : 0;
    --self->count;
    self->mem -= msgsz;
    if (!self->count)
        result |= SP_MSGQUEUE_RELEASE;
    
    sp_mutex_unlock (&self->sync);

    *len = msgsz;
    return result;
}

