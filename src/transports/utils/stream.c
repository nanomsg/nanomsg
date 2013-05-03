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

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/wire.h"
#include "../../utils/fast.h"

#include <string.h>
#include <stdint.h>

/*  Possible states of object. */
#define NN_STREAM_STATE_INIT 1
#define NN_STREAM_STATE_SENDING_PROTOHDR 2
#define NN_STREAM_STATE_RECEIVING_PROTOHDR 3
#define NN_STREAM_STATE_DISABLING_TIMER 4
#define NN_STREAM_STATE_ACTIVE 5
#define NN_STREAM_STATE_CLOSING_TIMER 6
#define NN_STREAM_STATE_CLOSED 7

/*  Possible states of the inbound part of the object. */
#define NN_STREAM_INSTATE_HDR 1
#define NN_STREAM_INSTATE_BODY 2
#define NN_STREAM_INSTATE_HASMSG 3

/*  Possible states of the outbound part of the object. */
#define NN_STREAM_OUTSTATE_IDLE 1
#define NN_STREAM_OUTSTATE_SENDING 2

/*  Inbound events. */
#define NN_STREAM_EVENT_START 1
#define NN_STREAM_EVENT_SEND 2
#define NN_STREAM_EVENT_RECV 3
#define NN_STREAM_EVENT_CLOSE 4

/*  Private functions. */
static void nn_stream_callback (struct nn_fsm *self, void *source, int type);

/*  Stream is a special type of pipe. Here it implements the pipe interface. */
static int nn_stream_send (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_stream_recv (struct nn_pipebase *self, struct nn_msg *msg);
const struct nn_pipebase_vfptr nn_stream_pipebase_vfptr = {
    nn_stream_send,
    nn_stream_recv
};

void nn_stream_init (struct nn_stream *self, struct nn_epbase *epbase,
    struct nn_fsm *owner)
{
    int rc;
    int protocol;
    size_t sz;

    /*  Initialise the state machine. */
    nn_fsm_init (&self->fsm, nn_stream_callback, owner);
    self->state = NN_STREAM_STATE_INIT;

    self->usock = NULL;
    self->usock_owner = NULL;

    /*  Initialise the pipe to communicate with the user. */
    rc = nn_pipebase_init (&self->pipebase, &nn_stream_pipebase_vfptr, epbase);
    nn_assert (rc == 0);

    nn_msg_init (&self->inmsg, 0);
    nn_msg_init (&self->outmsg, 0);

    nn_timer_init (&self->hdr_timeout, &self->fsm);

    nn_fsm_event_init (&self->event_error, self, NN_STREAM_ERROR);
    nn_fsm_event_init (&self->event_closed, self, NN_STREAM_CLOSED);

    /*  Prepare the outgoing protocol header.  */
    sz = sizeof (protocol);
    nn_epbase_getopt (epbase, NN_SOL_SOCKET, NN_PROTOCOL, &protocol, &sz);
    errnum_assert (rc == 0, -rc);
    nn_assert (sz == sizeof (protocol));
    memcpy (self->protohdr, "\0\0SP\0\0\0\0", 8);
    nn_puts (self->protohdr + 4, (uint16_t) protocol);
}

void nn_stream_term (struct nn_stream *self)
{
    /*  Sanity check. */
    nn_assert (self->state = NN_STREAM_STATE_INIT ||
        self->state == NN_STREAM_STATE_CLOSED);

    nn_fsm_event_term (&self->event_closed);
    nn_fsm_event_term (&self->event_error);

    nn_msg_term (&self->inmsg);
    nn_msg_term (&self->outmsg);
    nn_pipebase_term (&self->pipebase);
}

void nn_stream_start (struct nn_stream *self, struct nn_usock *usock)
{
    /*  Redirect the underlying socket's events to this state machine. */
    self->usock = usock;
    self->usock_owner = nn_usock_swap_owner (self->usock, &self->fsm);

    /*  Pass the event to the state machine. */
    nn_stream_callback (&self->fsm, NULL, NN_STREAM_EVENT_START);
}

void nn_stream_close (struct nn_stream *self)
{
    /*  Pass the appropriate event to the state machine. */
    nn_stream_callback (&self->fsm, NULL, NN_STREAM_EVENT_CLOSE);
}

static int nn_stream_send (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_stream *stream;

    stream = nn_cont (self, struct nn_stream, pipebase);

    /*  Move the message to the local storage. */
    nn_msg_term (&stream->outmsg);
    nn_msg_mv (&stream->outmsg, msg);

    /*  Pass the event to the state machine. */
    nn_stream_callback (&stream->fsm, NULL, NN_STREAM_EVENT_SEND);

    return 0;
}

static int nn_stream_recv (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_stream *stream;

    stream = nn_cont (self, struct nn_stream, pipebase);

    /*  Move received message to the user. */
    nn_msg_mv (msg, &stream->inmsg);
    nn_msg_init (&stream->inmsg, 0);

    /*  We can start receiving a new message now. Pass the event to the
        state machine. */
    nn_stream_callback (&stream->fsm, NULL, NN_STREAM_EVENT_RECV);

    return 0;
}

static void nn_stream_callback (struct nn_fsm *self, void *source, int type)
{
    struct nn_stream *stream;
    struct nn_iovec iovec;
    uint16_t protocol;
    struct nn_iovec iov [3];
    uint64_t size;

    stream = nn_cont (self, struct nn_stream, fsm);

    switch (stream->state) {

/******************************************************************************/
/*  INIT state.                                                               */
/*  Object is initialised, but the state machine wasn't yet started.          */
/******************************************************************************/
    case NN_STREAM_STATE_INIT:
        if (source == NULL) {
             switch (type) {
             case NN_STREAM_EVENT_START:

                /*  Start the header timeout timer. */
                nn_timer_start (&stream->hdr_timeout, 1000);

                /*  Send the protocol header. We don't event try to do sending
                    and receiving the header in parallel. The rationale is that
                    the outgoing header will fill into TCP tx buffer and thus
                    will be sent asynchronously anyway. */
                iovec.iov_base = stream->protohdr;
                iovec.iov_len = 8;
                nn_usock_send (stream->usock, &iovec, 1);
                stream->state = NN_STREAM_STATE_SENDING_PROTOHDR;
                return;

             default:
                 nn_assert (0);
             }
        }
        nn_assert (0);

/******************************************************************************/
/*  SENDING_PROTOHDR state.                                                   */
/*  Protocol header is being sent to the peer. Waiting till it's done.        */
/******************************************************************************/
    case NN_STREAM_STATE_SENDING_PROTOHDR:
        if (source == stream->usock) {
            switch (type) {
            case NN_USOCK_SENT:
                
                /*  Start receiving the protocol header from the peer. */
                nn_usock_recv (stream->usock, stream->protohdr, 8);
                stream->state = NN_STREAM_STATE_RECEIVING_PROTOHDR;
                return;

            case NN_USOCK_ERROR:

                /*  Close the header exchange timer and proceed with stream
                    shutdown. */
                nn_timer_close (&stream->hdr_timeout);
                stream->state = NN_STREAM_STATE_CLOSING_TIMER;
                return;

            default:
                nn_assert (0);
            }
        }
        if (source == &stream->hdr_timeout) {
            switch (type) {
            case NN_TIMER_TIMEOUT:

                /*  Close the header exchange timer and proceed with stream
                    shutdown. */
                nn_timer_close (&stream->hdr_timeout);
                stream->state = NN_STREAM_STATE_CLOSING_TIMER;
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  RECEIVING_PROTOHDR state.                                                 */
/*  Waiting for protocol header from the peer.                                */
/******************************************************************************/
    case NN_STREAM_STATE_RECEIVING_PROTOHDR:
        if (source == stream->usock) {
            switch (type) {
            case NN_USOCK_RECEIVED:

                /*  TODO: If it does not conform, drop the connection. */
                protocol = nn_gets (stream->protohdr + 4);
                if (!nn_pipebase_ispeer (&stream->pipebase, protocol))
                    nn_assert (0);

                /*  Close the header exchange timer. */
                nn_timer_close (&stream->hdr_timeout);
                stream->state = NN_STREAM_STATE_DISABLING_TIMER;

                return;

            case NN_USOCK_ERROR:

                /*  Close the header exchange timer and proceed with stream
                    shutdown. */
                nn_timer_close (&stream->hdr_timeout);
                stream->state = NN_STREAM_STATE_CLOSING_TIMER;
                return;

            default:
                nn_assert (0);
            }
        }
        if (source == &stream->hdr_timeout) {
            switch (type) {
            case NN_TIMER_TIMEOUT:

                /*  Close the header exchange timer and proceed with stream
                    shutdown. */
                nn_timer_close (&stream->hdr_timeout);
                stream->state = NN_STREAM_STATE_CLOSING_TIMER;
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  DISABLING_TIMER state.                                                    */
/*  After doing the initial protocol header exchange we don't need the timer  */
/*  any more. Here we are closing it. Afterwards we'll proceed to ACTIVE      */
/*  state.                                                                    */
/******************************************************************************/
    case NN_STREAM_STATE_DISABLING_TIMER:
        if (source == &stream->hdr_timeout) {
            switch (type) {
            case NN_TIMER_CLOSED:

                /*  Connection is ready for sending. Make outpipe available
                    to the SP socket. */
                stream->outstate = NN_STREAM_OUTSTATE_IDLE;
                nn_pipebase_activate (&stream->pipebase);

                /*  Start waiting for incoming messages.
                    First, read the 8-byte size. */
                stream->instate = NN_STREAM_INSTATE_HDR;
                nn_usock_recv (stream->usock, stream->inhdr, 8);

                stream->state = NN_STREAM_STATE_ACTIVE;

                return;
            default:
                nn_assert (0);
            }
        }
        if (source == stream->usock) {
            switch (type) {
            case NN_USOCK_ERROR:

                /*  We'll continue closing the timer, but now we'll proceed
                    with the shutdown afterwards, instead of switching to the
                    ACTIVE state. */
                stream->state = NN_STREAM_STATE_CLOSING_TIMER;
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state                                                              */
/*  In this state the object is actively sending and receiving messages.      */
/******************************************************************************/
    case NN_STREAM_STATE_ACTIVE:
        if (source == NULL) {
            switch (type) {
            case NN_STREAM_EVENT_SEND:

                /*  User sends a message. */
                nn_assert (stream->outstate == NN_STREAM_OUTSTATE_IDLE);

                /*  Serialise the message header. */
                nn_putll (stream->outhdr,
                    nn_chunkref_size (&stream->outmsg.hdr) +
                    nn_chunkref_size (&stream->outmsg.body));

                /*  Start async sending. */
                iov [0].iov_base = stream->outhdr;
                iov [0].iov_len = sizeof (stream->outhdr);
                iov [1].iov_base = nn_chunkref_data (&stream->outmsg.hdr);
                iov [1].iov_len = nn_chunkref_size (&stream->outmsg.hdr);
                iov [2].iov_base = nn_chunkref_data (&stream->outmsg.body);
                iov [2].iov_len = nn_chunkref_size (&stream->outmsg.body);;
                nn_usock_send (stream->usock, iov, 3);
                stream->outstate = NN_STREAM_OUTSTATE_SENDING;

                return;

            case NN_STREAM_EVENT_RECV:

                /*  User asks to receive a message. */
                nn_assert (stream->instate == NN_STREAM_INSTATE_HASMSG);

                /*  Start receiving new message. */
                stream->instate = NN_STREAM_INSTATE_HDR;
                nn_usock_recv (stream->usock, stream->inhdr, 8);
                
                return;

            case NN_STREAM_EVENT_CLOSE:

                /*  User asks the stream to close. Return control of
                    the underlying socket to the owner and notify it about
                    the fact. */
                nn_usock_swap_owner (stream->usock, stream->usock_owner);
                stream->usock = NULL;
                stream->usock_owner = NULL;
                stream->state = NN_STREAM_STATE_CLOSED;
                nn_fsm_raise (&stream->fsm, &stream->event_closed);

                return;

            default:
                nn_assert (0);
            }
        }
        if (source == stream->usock) {
            switch (type) {
            case NN_USOCK_SENT:
                nn_assert (stream->outstate == NN_STREAM_OUTSTATE_SENDING);
                stream->outstate = NN_STREAM_OUTSTATE_IDLE;
                nn_pipebase_sent (&stream->pipebase);
                return;
            case NN_USOCK_RECEIVED:
                switch (stream->instate) {
                case NN_STREAM_INSTATE_HDR:

                    /*  Message header was received. Allocate memory for the
                        message. */
                    size = nn_getll (stream->inhdr);
                    nn_msg_term (&stream->inmsg);
                    nn_msg_init (&stream->inmsg, (size_t) size);

                    /*  Special case when size of the message body is 0. */
                    if (!size) {
                        nn_pipebase_received (&stream->pipebase);
                        nn_assert (0);
                    }

                    /*  Start receiving the message body. */
                    stream->instate = NN_STREAM_INSTATE_BODY;
                    nn_usock_recv (stream->usock,
                        nn_chunkref_data (&stream->inmsg.body), (size_t) size);

                    return;

                case NN_STREAM_INSTATE_BODY:

                    /*  Message body was received. Notify the owner that it
                        can receive it. */
                    stream->instate = NN_STREAM_INSTATE_HASMSG;
                    nn_pipebase_received (&stream->pipebase);

                    return;

                default:
                    nn_assert (0);
                }
            case NN_USOCK_ERROR:

                /*  Close the stream. */
                nn_usock_swap_owner (stream->usock, stream->usock_owner);
                stream->usock = NULL;
                stream->usock_owner = NULL;
                stream->state = NN_STREAM_STATE_CLOSED;
                nn_fsm_raise (&stream->fsm, &stream->event_closed);
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_TIMER state.                                                      */
/*  Protocol header exchange have failed. We are now closing the timer and    */
/*  we will proceed to the CLOSED state once it is done.                      */
/******************************************************************************/
    case NN_STREAM_STATE_CLOSING_TIMER:
        if (source == &stream->hdr_timeout) {
            switch (type) {
            case NN_TIMER_CLOSED:
                nn_usock_swap_owner (stream->usock, stream->usock_owner);
                stream->usock = NULL;
                stream->usock_owner = NULL;
                stream->state = NN_STREAM_STATE_CLOSED;
                nn_fsm_raise (&stream->fsm, &stream->event_closed);
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        if (source == stream->usock) {
            switch (type) {
            case NN_USOCK_ERROR:

                /*  We are closing the stream anyway. Nothing to do here. */
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);
/******************************************************************************/
/*  CLOSED state.                                                             */
/******************************************************************************/
    case NN_STREAM_STATE_CLOSED:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

