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

#ifndef NN_STREAM_INCLUDED
#define NN_STREAM_INCLUDED

#include "../transport.h"

#include "aio.h"
#include "msg.h"

#include <stdint.h>

/*  Session object for stream-oriented transports (e.g. TCP or IPC). */

#define NN_STREAM_INSTATE_HDR 1
#define NN_STREAM_INSTATE_BODY 2

struct nn_stream {

    /*  Event sink. */
    const struct nn_cp_sink *sink;

    /*  Pipe to exchange messages with the user of the library. */
    struct nn_pipebase pipebase;

    /*  The underlying socket. */
    struct nn_usock *usock;

    /*  Protocol header. */
    uint8_t protohdr [8];

    /*  If header is not received in certain amount of time, connection is
        closed. This solves a rare race condition in TCP. It also minimises
        the usage of resources in case of erroneous connections. Also, it
        prevents a simple DoS attack. */
    struct nn_timer hdr_timeout;

    /*  State of the inbound state machine. */
    int instate;

    /*  Buffer used to store the header of incoming message. */
    uint8_t inhdr [8];

    /*  Message being received at the moment. */
    struct nn_msg inmsg;

    /*  State of the outbound state machine. */
    int outstate;

    /*  Buffer used to store the header of outgoing message. */
    uint8_t outhdr [8];

    /*  Message being sent at the moment. */
    struct nn_msg outmsg;

    /*  Stores the sink of the parent state machine while this state machine
        does its job. */
    const struct nn_cp_sink **original_sink;
};

void nn_stream_init (struct nn_stream *self, struct nn_epbase *epbase,
    struct nn_usock *usock);
void nn_stream_term ();

#endif
