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

#ifndef SP_STREAM_INCLUDED
#define SP_STREAM_INCLUDED

#include "../transport.h"

#include "aio.h"
#include "msg.h"

#include <stdint.h>

/*  Session object for stream-oriented transports (e.g. TCP or IPC). */

#define SP_STREAM_INSTATE_HDR 1
#define SP_STREAM_INSTATE_BODY 2

#define SP_STREAM_OUTSTATE_HDR 1
#define SP_STREAM_OUTSTATE_BODY 2

struct sp_stream {

    /*  Event sink. */
    const struct sp_cp_sink *sink;

    /*  Pipe to exchange messages with the user of the library. */
    struct sp_pipebase pipebase;

    /*  The underlying socket. */
    struct sp_usock *usock;

    /*  Protocol header received from the peer. */
    uint8_t hdr [8];

    /*  If header is not received in certain amount of time, connection is
        closed. This solves a rare race condition in TCP. It also minimises
        the usage of resources in case of erroneous connections. Also, it
        prevents a simple DoS attack. */
    struct sp_timer hdr_timeout;

    /*  State of the inbound state machine. */
    int instate;

    /*  Buffer used to store the header of incoming message. */
    uint8_t inhdr [8];

    /*  Message being received at the moment. */
    struct sp_msgref inmsg;

    /*  State of the outbound state machine. */
    int outstate;

    /*  Buffer for the header of outgoing message. */
    uint8_t outhdr [8];

    /*  Message being sent at the moment. */
    struct sp_msgref outmsg;

    /*  Stores the sink of the parent state machine while this state machine
        does its job. */
    const struct sp_cp_sink **original_sink;
};

void sp_stream_init (struct sp_stream *self, struct sp_epbase *epbase,
    struct sp_usock *usock);
void sp_stream_term ();

#endif
