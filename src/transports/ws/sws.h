/*
    Copyright (c) 2013 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.

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

#ifndef NN_SWS_INCLUDED
#define NN_SWS_INCLUDED

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "wshdr.h"

#include "../../utils/msg.h"
#include "../../utils/list.h"

/*  This state machine handles WebSocket connection from the point where it is
    established to the point when it is broken. */

/*  Return codes of this state machine. */
#define NN_SWS_RETURN_ERROR 1
#define NN_SWS_RETURN_CLOSE_HANDSHAKE 2
#define NN_SWS_RETURN_STOPPED 3

/*  WebSocket protocol header frame sizes. */
#define NN_SWS_FRAME_SIZE_INITIAL 2
#define NN_SWS_FRAME_SIZE_PAYLOAD_0 0
#define NN_SWS_FRAME_SIZE_PAYLOAD_16 2
#define NN_SWS_FRAME_SIZE_PAYLOAD_63 8

/*  WebSocket control bitmasks as per RFC 6455 5.2. */
#define NN_SWS_FRAME_BITMASK_FIN 0x80
#define NN_SWS_FRAME_BITMASK_RSV1 0x40
#define NN_SWS_FRAME_BITMASK_RSV2 0x20
#define NN_SWS_FRAME_BITMASK_RSV3 0x10
#define NN_SWS_FRAME_BITMASK_OPCODE 0x0F

/*  The longest possible header frame length. As per RFC 6455 5.2:
    first 2 bytes of initial framing + up to 8 bytes of additional
    extended payload length header + 4 byte mask = 14bytes
    Not all messages will use the maximum amount allocated, but we are
    statically allocating this buffer for convenience. */
#define NN_SWS_FRAME_MAX_HDR_LEN 14

/*  Maximal payload size for messages with 7-bit size field. */
#define NN_SWS_MAX_SMALL_PAYLOAD 0x7d

/*  WebSocket Close Status Code length. */
#define NN_SWS_CLOSE_CODE_LEN 2

struct nn_sws {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  Controls framing based on whether this peer is acting as
        a Client or a Server. */
    int mode;

    /*  The underlying socket. */
    struct nn_usock *usock;

    /*  Child state machine to do protocol header exchange. */
    struct nn_wshdr wshdr;

    /*  The original owner of the underlying socket. */
    struct nn_fsm_owner usock_owner;

    /*  Pipe connecting this WebSocket connection to the nanomsg core. */
    struct nn_pipebase pipebase;

    /*  Requested resource when acting as client. */
    const char* resource;

    /*  Remote Host in header request when acting as client. */
    const char* remote_host;

    /*  State of inbound state machine. */
    int instate;

    /*  Buffer used to store the framing of incoming message. */
    uint8_t inhdr [NN_SWS_FRAME_MAX_HDR_LEN];

    /*  Parsed header frames. */
    uint8_t opcode;
    uint8_t payload_ctl;
    uint8_t masked;
    uint8_t *mask;
    size_t ext_hdr_len;
    int is_final_frame;
    int is_control_frame;

    /*  As valid fragments are being received, this flag stays true until
        the FIN bit is received. This state is also used to determine
        peer sequencing anamolies that trigger this endpoint to fail the
        connection. */
    int continuing;

    /*  Fragments of message being received at the moment. */
    struct nn_list inmsg_array;
    uint8_t *inmsg_current_chunk_buf;
    size_t inmsg_current_chunk_len;
    size_t inmsg_total_size;
    int inmsg_chunks;
    uint8_t inmsg_hdr;

    /*  Control message being received at the moment. Because these can be
        interspersed between fragmented payload messages, they are
        stored in this buffer so as not to interrupt the message array. */
    uint8_t inmsg_control [NN_SWS_MAX_SMALL_PAYLOAD];

    /*  Reason this connection is closing to send as closing handshake. */
    char fail_msg [NN_SWS_MAX_SMALL_PAYLOAD];
    size_t fail_msg_len;

    /*  State of the outbound state machine. */
    int outstate;

    /*  Buffer used to store the header of outgoing message. */
    uint8_t outhdr [NN_SWS_FRAME_MAX_HDR_LEN];

    /*  Message being sent at the moment. */
    struct nn_msg outmsg;

    /*  Event raised when the state machine ends. */
    struct nn_fsm_event done;
};

void nn_sws_init (struct nn_sws *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner);
void nn_sws_term (struct nn_sws *self);
int nn_sws_isidle (struct nn_sws *self);
void nn_sws_start (struct nn_sws *self, struct nn_usock *usock, int mode,
    const char *resource, const char *host);
void nn_sws_stop (struct nn_sws *self);

#endif

