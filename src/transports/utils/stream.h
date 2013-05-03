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

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/timer.h"
#include "../../aio/usock.h"

#include "../../utils/msg.h"

#include <stdint.h>

/*  Session object for stream-oriented transports (e.g. TCP or IPC). */

/*  Events generate by the stream object. CLOSED event has deliberately
    complex code so that bstream can better detect possible invalid events
    when waiting for shut down of all associated streams. */
#define NN_STREAM_ERROR 1
#define NN_STREAM_CLOSED 10283

struct nn_stream {

    /*  State machine. */
    struct nn_fsm fsm;
    int state;

    /*  Pipe to exchange messages with the user of the library. */
    struct nn_pipebase pipebase;

    /*  The underlying socket and its original owner. */
    struct nn_usock *usock;
    struct nn_fsm *usock_owner;

    /*  Protocol header. */
    uint8_t protohdr [8];

    /*  If header is not received in certain amount of time, connection is
        closed. This solves a rare race condition in TCP. It also minimises
        the usage of resources in case of erroneous connections. Also, it
        prevents a simple DoS attack. */
    struct nn_timer hdr_timeout;

    /*  State of inbound state machine. */
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

    /*  Event to be sent to the owner. */
    struct nn_fsm_event event_error;
    struct nn_fsm_event event_closed;
};

/*  Initialise the object. */
void nn_stream_init (struct nn_stream *self, struct nn_epbase *epbase,
    struct nn_fsm *owner);

/*  Starts the state machine. The socket passed to this function has to be
    already connected. The object will grab ownership of the socket and return
    it to the caller only once the connection is closed. */
void nn_stream_start (struct nn_stream *self, struct nn_usock *usock);

/*  Ask object to close. When done, CLOSE event will be triggered. */
void nn_stream_close (struct nn_stream *self);

/*  Deallocate the object. Call this function only once the CLOSE or ERROR
    event was received. */
void nn_stream_term (struct nn_stream *self);

#endif
