/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#ifndef SP_MSGQUEUE_INCLUDED
#define SP_MSGQUEUE_INCLUDED

#include "../../utils/msg.h"
#include "../../utils/mutex.h"

#include <stddef.h>

/*  This class is a simple uni-directional message queue. */
/*  TODO: Recv-side batching can be implemented here to improve performance. */

/*  This flag is returned from send/recv functions to let the user know that
    more sends/recvs are not possible. */
#define SP_MSGQUEUE_RELEASE 1

/*  This flag is returned from send/recv functions to let the user know that
    other side of the pipe should be re-activated. */
#define SP_MSGQUEUE_SIGNAL 2

/*  It's not 128 so that chunk including its footer fits into a memory page. */
#define SP_MSGQUEUE_GRANULARITY 127

struct sp_msgqueue_chunk {
    struct sp_msg msgs [SP_MSGQUEUE_GRANULARITY];
    struct sp_msgqueue_chunk *next;
};

struct sp_msgqueue {

    /*  Pointer to the position where next message should be written into
        the message queue. */
    struct {
        struct sp_msgqueue_chunk *chunk;
        int pos;
    } out;

    /*  Pointer to the first unread message in the message queue. */
    struct {
        struct sp_msgqueue_chunk *chunk;
        int pos;
    } in;

    /*  Number of messages in the queue. */
    size_t count;

    /*  Amount of memory used by messages in the queue. */
    size_t mem;

    /*   Maximal queue size (in bytes). */
    size_t maxmem;

    /*  Synchronise passing messages from the writer thread to the reader
        thread. The 'out' structure is guarded as well as the cached chunk.
        'in' structure is local to the reader thread and is not synchronised. */
    struct sp_mutex sync;

    /*  One empty chunk is always cached so that in case of steady stream
        of messages through the pipe there are no memory allocations. The chunk
        is being handed from the reading thread to the writing thread so it
        is guarded by the 'sync' mutex. */
    struct sp_msgqueue_chunk *cache;

};

/*  Initialise the message pipe. maxmem is the maximal queue size in bytes. */
void sp_msgqueue_init (struct sp_msgqueue *self, size_t maxmem);

/*  Terminate the message pipe. */
void sp_msgqueue_term (struct sp_msgqueue *self);

/*  Writes a message to the pipe. -EAGAIN is returned if the message cannot
    be sent because the queue is full. 0 is returned in case of success. If,
    additionally, this makes the queue readable, the return value is 1. */
int sp_msgqueue_send (struct sp_msgqueue *self,
        const void *buf1, size_t len1, const void *buf2, size_t len2);

/*  Reads a message from the pipe. -EAGAIN is returned if there's no message
    to receive. 0 is returned in case of success. If, additionally, this makes
    the queue writeable, the return value is 1. 'len' is inout parameter --
    it contains the size of the buffer on input and actual size of the message
    on output. */
int sp_msgqueue_recv (struct sp_msgqueue *self, void *buf, size_t *len);

#endif
