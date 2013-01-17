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

#include "stream.h"

#include "err.h"
#include "cont.h"
#include "wire.h"
#include "fast.h"

#include <string.h>
#include <stdint.h>

/*   Private functions. */
static void sp_stream_hdr_received (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_stream_hdr_sent (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_stream_hdr_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static void sp_stream_activate (struct sp_stream *self);
static void sp_stream_received (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_stream_sent (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static void sp_stream_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);

/*  START state. */
static const struct sp_cp_sink sp_stream_state_start = {
    sp_stream_hdr_received,
    sp_stream_hdr_sent,
    NULL,
    NULL,
    sp_stream_err,
    NULL,
    sp_stream_hdr_timeout,
    NULL
};

/*  SENT state. */
static const struct sp_cp_sink sp_stream_state_sent = {
    sp_stream_hdr_received,
    NULL,
    NULL,
    NULL,
    sp_stream_err,
    NULL,
    sp_stream_hdr_timeout,
    NULL
};

/*  RECEIVED state. */
static const struct sp_cp_sink sp_stream_state_received = {
    NULL,
    sp_stream_hdr_sent,
    NULL,
    NULL,
    sp_stream_err,
    NULL,
    sp_stream_hdr_timeout,
    NULL
};

/*  ACTIVE state. */
static const struct sp_cp_sink sp_stream_state_active = {
    sp_stream_received,
    sp_stream_sent,
    NULL,
    NULL,
    sp_stream_err,
    NULL,
    NULL,
    NULL
};

/*  Pipe interface. */
static void sp_stream_send (struct sp_pipebase *self, struct sp_msg *msg);
static void sp_stream_recv (struct sp_pipebase *self, void *buf, size_t *len);
const struct sp_pipebase_vfptr sp_stream_pipebase_vfptr = {
    sp_stream_send,
    sp_stream_recv
};

void sp_stream_init (struct sp_stream *self, struct sp_epbase *epbase,
    struct sp_usock *usock)
{
    int rc;
    struct sp_iovec iov;

    /*  Redirect the underlying socket's events to this state machine. */
    self->usock = usock;
    self->sink = &sp_stream_state_start;
    self->original_sink = sp_usock_setsink (usock, &self->sink);

    /*  Initialise the pipe to communicate with the user. */
    /*  TODO: Socket type may reject the pipe. What then? */
    rc = sp_pipebase_init (&self->pipebase, &sp_stream_pipebase_vfptr, epbase);
    sp_assert (rc == 0);

    /*  Start the header timeout timer. */
    sp_timer_init (&self->hdr_timeout, &self->sink, usock->cp);
    sp_timer_start (&self->hdr_timeout, 1000);

    /*  Send the protocol header. */
    iov.iov_base = "\0\0SP\0\0\0\0";
    iov.iov_len = 8;
    sp_usock_send (usock, &iov, 1);

    /*  Receive the protocol header from the peer. */
    sp_usock_recv (usock, self->hdr, 8);
}

void sp_stream_term (struct sp_stream *self)
{
    /*  TODO:  Close the messages in progress. */
    sp_timer_term (&self->hdr_timeout);
    sp_pipebase_term (&self->pipebase);

    /*  Return control to the parent state machine. */
    sp_usock_setsink (self->usock, self->original_sink);
}

static void sp_stream_hdr_received (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_stream *stream;

    stream = sp_cont (self, struct sp_stream, sink);

    if (stream->sink == &sp_stream_state_sent) {
        sp_stream_activate (stream);
        return;
    }

    if (stream->sink == &sp_stream_state_start) {
        stream->sink = &sp_stream_state_received;
        return;
    }

    /*  This event is not defined in other states. */
    sp_assert (0);
}

static void sp_stream_hdr_sent (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_stream *stream;

    stream = sp_cont (self, struct sp_stream, sink);

    if (stream->sink == &sp_stream_state_received) {
        sp_stream_activate (stream);
        return;
    }

    if (stream->sink == &sp_stream_state_start) {
        stream->sink = &sp_stream_state_sent;
        return;
    }

    /*  This event is not defined in other states. */
    sp_assert (0);
}

static void sp_stream_hdr_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    struct sp_stream *stream;
    const struct sp_cp_sink **original_sink;

    /*  The initial protocol header exchange have timed out. */
    stream = sp_cont (self, struct sp_stream, sink);
    original_sink = stream->original_sink;

    /*  Terminate the session object. */
    sp_stream_term (stream);

    /*  Notify the parent state machine about the failure. */
    sp_assert ((*original_sink)->err);
    (*original_sink)->err (original_sink, stream->usock, ETIMEDOUT);
}

static void sp_stream_activate (struct sp_stream *self)
{
    self->sink = &sp_stream_state_active;
    sp_timer_stop (&self->hdr_timeout);

    /*  Check the header. */
    /*  TODO: If it does not conform, drop the connection. */
    if (memcmp (self->hdr, "\0\0SP\0\0\0\0", 8) != 0)
        sp_assert (0);

    /*  Connection is ready for sending. Make outpipe available
        to the SP socket. */
    sp_pipebase_activate (&self->pipebase);

    /*  Start waiting for incoming messages. First, read the 8-byte size. */
    self->instate = SP_STREAM_INSTATE_HDR;
    sp_usock_recv (self->usock, self->inhdr, 8);
}

static void sp_stream_received (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    int rc;
    struct sp_stream *stream;
    uint64_t size;

    stream = sp_cont (self, struct sp_stream, sink);
    switch (stream->instate) {
    case SP_STREAM_INSTATE_HDR:
        size = sp_getll (stream->inhdr);
        sp_msg_init (&stream->inmsg, (size_t) size);
        if (!size) {
            sp_pipebase_received (&stream->pipebase);
            break;
        }
        stream->instate = SP_STREAM_INSTATE_BODY;
        sp_usock_recv (stream->usock, sp_chunkref_data (&stream->inmsg.body),
            (size_t) size);
        break;
    case SP_STREAM_INSTATE_BODY:
        sp_pipebase_received (&stream->pipebase);
        break;
    default:
        sp_assert (0);
    }
}

static void sp_stream_sent (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_stream *stream;

    stream = sp_cont (self, struct sp_stream, sink);
    sp_pipebase_sent (&stream->pipebase);
    sp_msg_term (&stream->outmsg);
}

static void sp_stream_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    struct sp_stream *stream;
    const struct sp_cp_sink **original_sink;

    stream = sp_cont (self, struct sp_stream, sink);
    original_sink = stream->original_sink;

    /*  Terminate the session object. */
    sp_stream_term (stream);

    /*  Notify the parent state machine about the failure. */
    sp_assert ((*original_sink)->err);
    (*original_sink)->err (original_sink, usock, errnum);
}

static void sp_stream_send (struct sp_pipebase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_stream *stream;
    struct sp_iovec iov [3];

    stream = sp_cont (self, struct sp_stream, pipebase);

    /*  Mave the message to the local storage. */
    sp_msg_mv (&stream->outmsg, msg);

    /*  Serialise the message header. */
    sp_putll (stream->outhdr, sp_chunkref_size (&stream->outmsg.hdr) +
        sp_chunkref_size (&stream->outmsg.body));

    /*  Start async sending. */
    iov [0].iov_base = stream->outhdr;
    iov [0].iov_len = sizeof (stream->outhdr);
    iov [1].iov_base = sp_chunkref_data (&stream->outmsg.hdr);
    iov [1].iov_len = sp_chunkref_size (&stream->outmsg.hdr);
    iov [2].iov_base = sp_chunkref_data (&stream->outmsg.body);
    iov [2].iov_len = sp_chunkref_size (&stream->outmsg.body);;
    sp_usock_send (stream->usock, iov, 3);
}

static void sp_stream_recv (struct sp_pipebase *self, void *buf, size_t *len)
{
    struct sp_stream *stream;
    size_t sz;

    stream = sp_cont (self, struct sp_stream, pipebase);

    /*  Copy the data to the supplied buffer. */
    sz = sp_chunkref_size (&stream->inmsg.body);
    if (*len < sz)
        *len = sz;
    memcpy (buf, sp_chunkref_data (&stream->inmsg.body), sz);

    /* Start receiving new message. */ 
    stream->instate = SP_STREAM_INSTATE_HDR;
    sp_usock_recv (stream->usock, stream->inhdr, 8);
}

