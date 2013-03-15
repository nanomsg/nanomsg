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
static void nn_stream_hdr_received (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static void nn_stream_hdr_sent (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static void nn_stream_hdr_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer);
static void nn_stream_received (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static void nn_stream_sent (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static void nn_stream_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum);

/*  START state. */
static const struct nn_cp_sink nn_stream_state_start = {
    NULL,
    nn_stream_hdr_sent,
    NULL,
    NULL,
    nn_stream_err,
    NULL,
    nn_stream_hdr_timeout,
    NULL
};

/*  SENT state. */
static const struct nn_cp_sink nn_stream_state_sent = {
    nn_stream_hdr_received,
    NULL,
    NULL,
    NULL,
    nn_stream_err,
    NULL,
    nn_stream_hdr_timeout,
    NULL
};

/*  ACTIVE state. */
static const struct nn_cp_sink nn_stream_state_active = {
    nn_stream_received,
    nn_stream_sent,
    NULL,
    NULL,
    nn_stream_err,
    NULL,
    NULL,
    NULL
};

/*  Pipe interface. */
static int nn_stream_send (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_stream_recv (struct nn_pipebase *self, struct nn_msg *msg);
const struct nn_pipebase_vfptr nn_stream_pipebase_vfptr = {
    nn_stream_send,
    nn_stream_recv
};

void nn_stream_init (struct nn_stream *self, struct nn_epbase *epbase,
    struct nn_usock *usock)
{
    int rc;
    int protocol;
    size_t sz;
    struct nn_iobuf iobuf;

    /*  Redirect the underlying socket's events to this state machine. */
    self->usock = usock;
    self->sink = &nn_stream_state_start;
    self->original_sink = nn_usock_setsink (usock, &self->sink);

    /*  Initialise the pipe to communicate with the user. */
    rc = nn_pipebase_init (&self->pipebase, &nn_stream_pipebase_vfptr, epbase);
    nn_assert (rc == 0);

    nn_msg_init (&self->inmsg, 0);
    nn_msg_init (&self->outmsg, 0);

    /*  Start the header timeout timer. */
    nn_timer_init (&self->hdr_timeout, &self->sink, usock->cp);
    nn_timer_start (&self->hdr_timeout, 1000);

    /*  Send the protocol header. */
    sz = sizeof (protocol);
    nn_epbase_getopt (epbase, NN_SOL_SOCKET, NN_PROTOCOL, &protocol, &sz);
    errnum_assert (rc == 0, -rc);
    nn_assert (sz == sizeof (protocol));
    memcpy (self->protohdr, "\0\0SP\0\0\0\0", 8);
    nn_puts (self->protohdr + 4, (uint16_t) protocol);
    iobuf.iov_base = self->protohdr;
    iobuf.iov_len = 8;
    nn_usock_send (usock, &iobuf, 1);
}

void nn_stream_term (struct nn_stream *self)
{
    /*  Close the messages in progress. */
    nn_msg_term (&self->inmsg);
    nn_msg_term (&self->outmsg);

    nn_timer_term (&self->hdr_timeout);
    nn_pipebase_term (&self->pipebase);

    /*  Return control to the parent state machine. */
    nn_usock_setsink (self->usock, self->original_sink);
}

static void nn_stream_hdr_sent (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_stream *stream;

    stream = nn_cont (self, struct nn_stream, sink);

    stream->sink = &nn_stream_state_sent;

    /*  Receive the protocol header from the peer. */
    nn_usock_recv (usock, stream->protohdr, 8);
}

static void nn_stream_hdr_received (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_stream *stream;
    int protocol;

    stream = nn_cont (self, struct nn_stream, sink);

    stream->sink = &nn_stream_state_active;
    nn_timer_stop (&stream->hdr_timeout);

    /*  TODO: If it does not conform, drop the connection. */
    protocol = nn_gets (stream->protohdr + 4);
    if (!nn_pipebase_ispeer (&stream->pipebase, protocol))
        nn_assert (0);

    /*  Connection is ready for sending. Make outpipe available
        to the SP socket. */
    nn_pipebase_activate (&stream->pipebase);

    /*  Start waiting for incoming messages. First, read the 8-byte size. */
    stream->instate = NN_STREAM_INSTATE_HDR;
    nn_usock_recv (stream->usock, stream->inhdr, 8);
}

static void nn_stream_hdr_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer)
{
    struct nn_stream *stream;
    const struct nn_cp_sink **original_sink;

    /*  The initial protocol header exchange have timed out. */
    stream = nn_cont (self, struct nn_stream, sink);
    original_sink = stream->original_sink;

    /*  Terminate the session object. */
    nn_stream_term (stream);

    /*  Notify the parent state machine about the failure. */
    nn_assert ((*original_sink)->err);
    (*original_sink)->err (original_sink, stream->usock, ETIMEDOUT);
}

static void nn_stream_received (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_stream *stream;
    uint64_t size;

    stream = nn_cont (self, struct nn_stream, sink);
    switch (stream->instate) {
    case NN_STREAM_INSTATE_HDR:
        size = nn_getll (stream->inhdr);
        nn_msg_term (&stream->inmsg);
        nn_msg_init (&stream->inmsg, (size_t) size);
        if (!size) {
            nn_pipebase_received (&stream->pipebase);
            break;
        }
        stream->instate = NN_STREAM_INSTATE_BODY;
        nn_usock_recv (stream->usock, nn_chunkref_data (&stream->inmsg.body),
            (size_t) size);
        break;
    case NN_STREAM_INSTATE_BODY:
        nn_pipebase_received (&stream->pipebase);
        break;
    default:
        nn_assert (0);
    }
}

static void nn_stream_sent (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_stream *stream;

    stream = nn_cont (self, struct nn_stream, sink);
    nn_pipebase_sent (&stream->pipebase);
    nn_msg_term (&stream->outmsg);
    nn_msg_init (&stream->outmsg, 0);
}

static void nn_stream_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum)
{
    struct nn_stream *stream;
    const struct nn_cp_sink **original_sink;

    stream = nn_cont (self, struct nn_stream, sink);
    original_sink = stream->original_sink;

    /*  Terminate the session object. */
    nn_stream_term (stream);

    /*  Notify the parent state machine about the failure. */
    nn_assert ((*original_sink)->err);
    (*original_sink)->err (original_sink, usock, errnum);
}

static int nn_stream_send (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_stream *stream;
    struct nn_iobuf iov [3];

    stream = nn_cont (self, struct nn_stream, pipebase);

    /*  Mave the message to the local storage. */
    nn_msg_term (&stream->outmsg);
    nn_msg_mv (&stream->outmsg, msg);

    /*  Serialise the message header. */
    nn_putll (stream->outhdr, nn_chunkref_size (&stream->outmsg.hdr) +
        nn_chunkref_size (&stream->outmsg.body));

    /*  Start async sending. */
    iov [0].iov_base = stream->outhdr;
    iov [0].iov_len = sizeof (stream->outhdr);
    iov [1].iov_base = nn_chunkref_data (&stream->outmsg.hdr);
    iov [1].iov_len = nn_chunkref_size (&stream->outmsg.hdr);
    iov [2].iov_base = nn_chunkref_data (&stream->outmsg.body);
    iov [2].iov_len = nn_chunkref_size (&stream->outmsg.body);;
    nn_usock_send (stream->usock, iov, 3);

    return 0;
}

static int nn_stream_recv (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_stream *stream;

    stream = nn_cont (self, struct nn_stream, pipebase);

    /*  Move message content to the user-supplied structure. */
    nn_msg_mv (msg, &stream->inmsg);
    nn_msg_init (&stream->inmsg, 0);

    /* Start receiving new message. */ 
    stream->instate = NN_STREAM_INSTATE_HDR;
    nn_usock_recv (stream->usock, stream->inhdr, 8);

    return 0;
}

