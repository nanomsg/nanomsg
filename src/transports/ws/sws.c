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

#include "sws.h"
#include "../../ws.h"

#include "../../utils/alloc.h"
#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/wire.h"
#include "../../utils/int.h"
#include "../../utils/attr.h"
#include "../../utils/random.h"

/*  States of the object as a whole. */
#define NN_SWS_STATE_IDLE 1
#define NN_SWS_STATE_HANDSHAKE 2
#define NN_SWS_STATE_STOPPING_HANDSHAKE 3
#define NN_SWS_STATE_ACTIVE 4
#define NN_SWS_STATE_CLOSING_CONNECTION 5
#define NN_SWS_STATE_BROKEN_CONNECTION 6
#define NN_SWS_STATE_DONE 7
#define NN_SWS_STATE_STOPPING 8

/*  Possible states of the inbound part of the object. */
#define NN_SWS_INSTATE_RECV_HDR 1
#define NN_SWS_INSTATE_RECV_HDREXT 2
#define NN_SWS_INSTATE_RECV_PAYLOAD 3
#define NN_SWS_INSTATE_RECVD_CHUNKED 4
#define NN_SWS_INSTATE_RECVD_CONTROL 5
#define NN_SWS_INSTATE_FAILING 6
#define NN_SWS_INSTATE_CLOSED 7

/*  Possible states of the outbound part of the object. */
#define NN_SWS_OUTSTATE_IDLE 1
#define NN_SWS_OUTSTATE_SENDING 2

/*  Subordinate srcptr objects. */
#define NN_SWS_SRC_USOCK 1
#define NN_SWS_SRC_HANDSHAKE 2

/*  WebSocket opcode constants as per RFC 6455 5.2. */
#define NN_WS_OPCODE_FRAGMENT 0x00
#define NN_WS_OPCODE_BINARY 0x02
#define NN_WS_OPCODE_CLOSE 0x08

/*  WebSocket protocol header bit masks as per RFC 6455. */
#define NN_SWS_FRAME_BITMASK_MASKED 0x80
#define NN_SWS_FRAME_BITMASK_NOT_MASKED 0x00
#define NN_SWS_FRAME_BITMASK_LENGTH 0x7F

/*  WebSocket Close Status Codes (1004-1006 and 1015 are reserved). */
#define NN_SWS_CLOSE_NORMAL 1000
#define NN_SWS_CLOSE_GOING_AWAY 1001
#define NN_SWS_CLOSE_ERR_PROTO 1002
#define NN_SWS_CLOSE_ERR_WUT 1003
#define NN_SWS_CLOSE_ERR_INVALID_FRAME 1007
#define NN_SWS_CLOSE_ERR_POLICY 1008
#define NN_SWS_CLOSE_ERR_TOOBIG 1009
#define NN_SWS_CLOSE_ERR_EXTENSION 1010
#define NN_SWS_CLOSE_ERR_SERVER 1011

/*  Scatter/gather array element type forincoming message chunks. Fragmented
    message frames are reassembled prior to notifying the user. */
struct msg_chunk {
    struct nn_list_item item;
    struct nn_chunkref chunk;
};

/*  Local functions. */
static void *nn_msg_chunk_new (size_t size, struct nn_list *msg_array);
static void nn_msg_chunk_term (struct msg_chunk *it,
    struct nn_list *msg_array);
static void nn_msg_array_term (struct nn_list *msg_array);

/*  Implementation of the virtual pipe API. */
static int nn_sws_send (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_sws_recv (struct nn_pipebase *self, struct nn_msg *msg);
const struct nn_pipebase_vfptr nn_sws_pipebase_vfptr = {
    nn_sws_send,
    nn_sws_recv
};

/*  Private functions. */
static void nn_sws_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_sws_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_sws_fail_conn (struct nn_sws *self, int code, char *reason);
static int nn_sws_recv_hdr (struct nn_sws *self);
static void nn_sws_mask_payload (uint8_t *payload, size_t payload_len,
    const uint8_t *mask, size_t mask_len, int *mask_start_pos);
static void nn_sws_validate_close_handshake (struct nn_sws *self);

void nn_sws_init (struct nn_sws *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_sws_handler, nn_sws_shutdown,
        src, self, owner);
    self->state = NN_SWS_STATE_IDLE;
    nn_wshdr_init (&self->wshdr, NN_SWS_SRC_HANDSHAKE, &self->fsm);
    self->usock = NULL;
    self->usock_owner.src = -1;
    self->usock_owner.fsm = NULL;
    nn_pipebase_init (&self->pipebase, &nn_sws_pipebase_vfptr, epbase);
    self->instate = -1;
    nn_list_init (&self->inmsg_array);
    self->outstate = -1;
    nn_msg_init (&self->outmsg, 0);

    self->continuing = 0;

    nn_fsm_event_init (&self->done);
}

void nn_sws_term (struct nn_sws *self)
{
    nn_assert_state (self, NN_SWS_STATE_IDLE);

    nn_fsm_event_term (&self->done);
    nn_msg_term (&self->outmsg);
    nn_msg_array_term (&self->inmsg_array);
    nn_pipebase_term (&self->pipebase);
    nn_wshdr_term (&self->wshdr);
    nn_fsm_term (&self->fsm);
}

int nn_sws_isidle (struct nn_sws *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_sws_start (struct nn_sws *self, struct nn_usock *usock, int mode,
    const char *resource, const char *host)
{
    /*  Take ownership of the underlying socket. */
    nn_assert (self->usock == NULL && self->usock_owner.fsm == NULL);
    self->usock_owner.src = NN_SWS_SRC_USOCK;
    self->usock_owner.fsm = &self->fsm;
    nn_usock_swap_owner (usock, &self->usock_owner);
    self->usock = usock;
    self->mode = mode;
    self->resource = resource;
    self->remote_host = host;

    /*  Launch the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_sws_stop (struct nn_sws *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_sws_shutdown (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_sws *sws;

    sws = nn_cont (self, struct nn_sws, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        /*  TODO: Consider sending a close code here? */
        nn_pipebase_stop (&sws->pipebase);
        nn_wshdr_stop (&sws->wshdr);
        sws->state = NN_SWS_STATE_STOPPING;
    }
    if (nn_slow (sws->state == NN_SWS_STATE_STOPPING)) {
        if (nn_wshdr_isidle (&sws->wshdr)) {
            nn_usock_swap_owner (sws->usock, &sws->usock_owner);
            sws->usock = NULL;
            sws->usock_owner.src = -1;
            sws->usock_owner.fsm = NULL;
            sws->state = NN_SWS_STATE_IDLE;
            nn_fsm_stopped (&sws->fsm, NN_SWS_RETURN_STOPPED);
            return;
        }
        return;
    }

    nn_fsm_bad_state (sws->state, src, type);
}

static void nn_sws_handler (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_sws *sws;
    int rc;

    sws = nn_cont (self, struct nn_sws, fsm);

    switch (sws->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_SWS_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_wshdr_start (&sws->wshdr, sws->usock,
                    &sws->pipebase, sws->mode, sws->resource, sws->remote_host);
                sws->state = NN_SWS_STATE_HANDSHAKE;
                return;
            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  HANDSHAKE state.                                                          */
/******************************************************************************/
    case NN_SWS_STATE_HANDSHAKE:
        switch (src) {

        case NN_SWS_SRC_HANDSHAKE:
            switch (type) {
            case NN_WSHDR_OK:

                /*  Before moving to the active state stop the handshake
                    state machine. */
                nn_wshdr_stop (&sws->wshdr);
                sws->state = NN_SWS_STATE_STOPPING_HANDSHAKE;
                return;

            case NN_WSHDR_ERROR:

                /* Raise the error and move directly to the DONE state.
                   wshdr object will be stopped later on. */
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done,
                    NN_SWS_RETURN_CLOSE_HANDSHAKE);
                return;

            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_HANDSHAKE state.                                                 */
/******************************************************************************/
    case NN_SWS_STATE_STOPPING_HANDSHAKE:
        switch (src) {

        case NN_SWS_SRC_HANDSHAKE:
            switch (type) {
            case NN_WSHDR_STOPPED:

                 /*  Start the pipe. */
                 rc = nn_pipebase_start (&sws->pipebase);
                 if (nn_slow (rc < 0)) {
                    sws->state = NN_SWS_STATE_DONE;
                    nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_RETURN_ERROR);
                    return;
                 }

                 /*  Start receiving a message in asynchronous manner. */
                 nn_sws_recv_hdr (sws);

                 /*  Mark the pipe as available for sending. */
                 sws->outstate = NN_SWS_OUTSTATE_IDLE;

                 sws->state = NN_SWS_STATE_ACTIVE;
                 return;

            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_SWS_STATE_ACTIVE:
        switch (src) {

        case NN_SWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SENT:

                /*  The message is now fully sent. */
                nn_assert (sws->outstate == NN_SWS_OUTSTATE_SENDING);
                sws->outstate = NN_SWS_OUTSTATE_IDLE;
                nn_msg_term (&sws->outmsg);
                nn_msg_init (&sws->outmsg, 0);
                nn_pipebase_sent (&sws->pipebase);
                return;

            case NN_USOCK_RECEIVED:

                switch (sws->instate) {
                case NN_SWS_INSTATE_RECV_HDR:

                    /*  Require RSV1, RSV2, and RSV3 bits to be unset for
                        x-nanomsg protocol as per RFC 6455 section 5.2. */
                    if (sws->inhdr [0] & NN_SWS_FRAME_BITMASK_RSV1 ||
                        sws->inhdr [0] & NN_SWS_FRAME_BITMASK_RSV2 ||
                        sws->inhdr [0] & NN_SWS_FRAME_BITMASK_RSV3) {
                        nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                            "RSV1, RSV2, and RSV3 must be unset.");
                        return;
                    }

                    sws->is_final_frame = sws->inhdr [0] &
                        NN_SWS_FRAME_BITMASK_FIN;
                    sws->masked = sws->inhdr [1] &
                        NN_SWS_FRAME_BITMASK_MASKED;

                    switch (sws->mode) {
                    case NN_WS_SERVER:
                        /*  Require mask bit to be set from client. */
                        if (sws->masked) {
                            /*  Continue receiving header for this frame. */
                            sws->ext_hdr_len = NN_SWS_FRAME_SIZE_MASK;
                            break;
                        }
                        else {
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Server expects MASK bit to be set.");
                            return;
                        }
                    case NN_WS_CLIENT:
                        /*  Require mask bit to be unset from server. */
                        if (sws->masked) {
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Client expects MASK bit to be unset.");
                            return;
                        }
                        else {
                            /*  Continue receiving header for this frame. */
                            sws->ext_hdr_len = 0;
                            break;
                        }
                    default:
                        /*  Only two modes of this endpoint are expected. */
                        nn_assert (0);
                        return;
                    }

                    sws->opcode = sws->inhdr [0] &
                        NN_SWS_FRAME_BITMASK_OPCODE;
                    sws->payload_ctl = sws->inhdr [1] &
                        NN_SWS_FRAME_BITMASK_LENGTH;

                    /*  Prevent unexpected continuation frame. */
                    if (!sws->continuing &&
                        sws->opcode == NN_WS_OPCODE_FRAGMENT) {
                        nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                            "No message to continue.");
                        return;
                    }

                    /*  Preserve initial message opcode and RSV bits in case
                        this is a fragmented message. */
                    if (!sws->continuing)
                        sws->inmsg_hdr = sws->inhdr [0] |
                        NN_SWS_FRAME_BITMASK_FIN;

                    if (sws->payload_ctl <= NN_SWS_PAYLOAD_MAX_LENGTH) {
                        sws->ext_hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_0;
                    }
                    else if (sws->payload_ctl == NN_SWS_PAYLOAD_FRAME_16) {
                        sws->ext_hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_16;
                    }
                    else if (sws->payload_ctl == NN_SWS_PAYLOAD_FRAME_63) {
                        sws->ext_hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_63;
                    }
                    else {
                        /*  Developer error parsing/handling length. */
                        nn_assert (0);
                        return;
                    }

                    switch (sws->opcode) {

                    case NN_WS_OPCODE_BINARY:

                        sws->is_control_frame = 0;

                        if (sws->continuing) {
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Expected continuation frame opcode.");
                            return;
                        }

                        if (!sws->is_final_frame)
                            sws->continuing = 1;

                        if (sws->ext_hdr_len == 0 && sws->payload_ctl == 0) {
                            /*  Only a remote server could send a 2-byte msg;
                                sanity-check that this endpoint is a client. */
                            nn_assert (sws->mode == NN_WS_CLIENT);

                            sws->inmsg_current_chunk_len = 0;

                            if (sws->continuing) {
                                /*  This frame was empty, but continue
                                    next frame in fragmented sequence. */
                                nn_sws_recv_hdr (sws);
                                return;
                            }
                            else {
                                /*  Special case when there is no payload,
                                    mask, or additional frames. */
                                sws->instate = NN_SWS_INSTATE_RECVD_CHUNKED;
                                nn_pipebase_received (&sws->pipebase);
                                return;
                            }
                            }
                        /*  Continue to receive extended header+payload. */
                        break;

                    case NN_WS_OPCODE_FRAGMENT:

                        sws->is_control_frame = 0;
                        sws->continuing = !sws->is_final_frame;

                        if (sws->ext_hdr_len == 0 && sws->payload_ctl == 0) {
                            /*  Only a remote server could send a 2-byte msg;
                                sanity-check that this endpoint is a client. */
                            nn_assert (sws->mode == NN_WS_CLIENT);

                            sws->inmsg_current_chunk_len = 0;

                            if (sws->continuing) {
                                /*  This frame was empty, but continue
                                    next frame in fragmented sequence. */
                                nn_sws_recv_hdr (sws);
                                return;
                            }
                            else {
                                /*  Special case when there is no payload,
                                    mask, or additional frames. */
                                sws->instate = NN_SWS_INSTATE_RECVD_CHUNKED;
                                nn_pipebase_received (&sws->pipebase);
                                return;
                            }
                        }
                        /*  Continue to receive extended header+payload. */
                        break;
                    
                    case NN_WS_OPCODE_CLOSE:
                        /*  RFC 6455 section 5.5.1. */
                        sws->is_control_frame = 1;
                        if (!sws->is_final_frame) {
                            /*  As per RFC 6455 section 5.4, fragmentation of
                                control frames is not allowed; on receipt the
                                endpoint MUST close connection immediately. */
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Cannot fragment control message (FIN=0).");
                            return;
                        }

                        if (sws->payload_ctl > NN_SWS_PAYLOAD_MAX_LENGTH) {
                            /*  As per RFC 6455 section 5.4, large payloads on
                                control frames is not allowed, and on receipt the
                                endpoint MUST close connection immediately. */
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Control frame payload exceeds allowable length.");
                            return;
                        }

                        if (sws->payload_ctl == 1) {
                            /*  As per RFC 6455 section 5.5.1, if a payload is
                                to accompany a close frame, the first two bytes
                                MUST be the close code. */
                            nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Expected 2byte close code.");
                            return;
                        }

                        if (sws->ext_hdr_len == 0 && sws->payload_ctl == 0) {
                            /*  Special case when there is no payload,
                                mask, or additional frames. */
                            sws->inmsg_current_chunk_len = 0;
                            sws->instate = NN_SWS_INSTATE_RECVD_CONTROL;
                            nn_pipebase_received (&sws->pipebase);
                            return;
                        }
                        /*  Continue to receive extended header+payload. */
                        break;
                    
                    default:
                        /*  Client sent an invalid opcode; as per RFC 6455
                            section 10.7, close connection with code. */
                        nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Invalid opcode.");
                        return;

                    }

                    if (sws->ext_hdr_len == 0) {
                        /*  Only a remote server could send a 2-byte msg;
                            sanity-check that this endpoint is a client. */
                        nn_assert (sws->mode == NN_WS_CLIENT);

                        /*  In the case of no additional header, the payload
                            is known to not exceed this threshold. */
                        nn_assert (sws->payload_ctl <= NN_SWS_PAYLOAD_MAX_LENGTH);

                        /*  In the case of no additional header, the payload
                            is known to not exceed this threshold. */
                        nn_assert (sws->payload_ctl > 0);

                        sws->instate = NN_SWS_INSTATE_RECV_PAYLOAD;
                        sws->inmsg_current_chunk_len = sws->payload_ctl;


                        /*  Use scatter/gather array for application messages,
                            and a fixed-width buffer for control messages. This
                            is convenient since control messages can be
                            interspersed between chunked application msgs. */
                        if (sws->is_control_frame) {
                            sws->inmsg_current_chunk_buf = sws->inmsg_control;
                        }
                        else {
                            sws->inmsg_chunks++;
                            sws->inmsg_total_size += sws->inmsg_current_chunk_len;
                            sws->inmsg_current_chunk_buf =
                                nn_msg_chunk_new (sws->inmsg_current_chunk_len,
                                &sws->inmsg_array);
                        }

                        nn_usock_recv (sws->usock, sws->inmsg_current_chunk_buf,
                            sws->inmsg_current_chunk_len, NULL);
                        return;
                    }
                    else {
                        /*  Continue receiving the rest of the header frame. */
                        sws->instate = NN_SWS_INSTATE_RECV_HDREXT;
                        nn_usock_recv (sws->usock,
                            sws->inhdr + NN_SWS_FRAME_SIZE_INITIAL,
                            sws->ext_hdr_len,
                            NULL);
                        return;
                    }

                case NN_SWS_INSTATE_RECV_HDREXT:
                    nn_assert (sws->ext_hdr_len > 0);

                    if (sws->payload_ctl <= NN_SWS_PAYLOAD_MAX_LENGTH) {
                        sws->inmsg_current_chunk_len = sws->payload_ctl;
                        if (sws->masked) {
                            sws->mask = sws->inhdr + NN_SWS_FRAME_SIZE_INITIAL;
                        }
                        else {
                            sws->mask = NULL;
                        }
                    }
                    else if (sws->payload_ctl == NN_SWS_PAYLOAD_FRAME_16) {
                        sws->inmsg_current_chunk_len =
                            nn_gets (sws->inhdr + NN_SWS_FRAME_SIZE_INITIAL);
                        if (sws->masked) {
                            sws->mask = sws->inhdr +
                                NN_SWS_FRAME_SIZE_INITIAL +
                                NN_SWS_FRAME_SIZE_PAYLOAD_16;
                        }
                        else {
                            sws->mask = NULL;
                        }
                    }
                    else if (sws->payload_ctl == NN_SWS_PAYLOAD_FRAME_63) {
                        sws->inmsg_current_chunk_len =
                            (size_t) nn_getll (sws->inhdr +
                            NN_SWS_FRAME_SIZE_INITIAL);
                        if (sws->masked) {
                            sws->mask = sws->inhdr +
                                NN_SWS_FRAME_SIZE_INITIAL +
                                NN_SWS_FRAME_SIZE_PAYLOAD_63;
                        }
                        else {
                            sws->mask = NULL;
                        }
                    }
                    else {
                        /*  Client sent invalid data; as per RFC 6455,
                            server closes the connection immediately. */
                        nn_sws_fail_conn (sws, NN_SWS_CLOSE_ERR_PROTO,
                                "Invalid payload length.");
                        return;
                    }

                    /*  Handle zero-length message bodies. */
                    if (sws->inmsg_current_chunk_len == 0)
                    {
                        if (sws->is_final_frame) {
                            sws->instate = (sws->is_control_frame ?
                                NN_SWS_INSTATE_RECVD_CONTROL :
                                NN_SWS_INSTATE_RECVD_CHUNKED);
                            nn_pipebase_received (&sws->pipebase);
                            return;
                        }
                        else {
                            nn_sws_recv_hdr (sws);
                            return;
                        }
                    }

                    nn_assert (sws->inmsg_current_chunk_len > 0);

                    /*  Use scatter/gather array for application messages,
                        and a fixed-width buffer for control messages. This
                        is convenient since control messages can be
                        interspersed between chunked application msgs. */
                    if (sws->is_control_frame) {
                        sws->inmsg_current_chunk_buf = sws->inmsg_control;
                    }
                    else {
                        sws->inmsg_chunks++;
                        sws->inmsg_total_size += sws->inmsg_current_chunk_len;
                        sws->inmsg_current_chunk_buf =
                            nn_msg_chunk_new (sws->inmsg_current_chunk_len,
                            &sws->inmsg_array);
                    }

                    sws->instate = NN_SWS_INSTATE_RECV_PAYLOAD;
                    nn_usock_recv (sws->usock, sws->inmsg_current_chunk_buf,
                        sws->inmsg_current_chunk_len, NULL);
                    return;

                case NN_SWS_INSTATE_RECV_PAYLOAD:

                    /*  Unmask if necessary. */
                    if (sws->masked) {
                        nn_sws_mask_payload (sws->inmsg_current_chunk_buf,
                            sws->inmsg_current_chunk_len, sws->mask,
                            NN_SWS_FRAME_SIZE_MASK, NULL);
                    }

                    switch (sws->opcode) {

                    case NN_WS_OPCODE_BINARY:
                        if (sws->is_final_frame) {
                            sws->instate = NN_SWS_INSTATE_RECVD_CHUNKED;
                            nn_pipebase_received (&sws->pipebase);
                        }
                        else {
                            nn_sws_recv_hdr (sws);
                        }
                        return;

                    case NN_WS_OPCODE_FRAGMENT:
                        if (sws->is_final_frame) {
                            sws->instate = NN_SWS_INSTATE_RECVD_CHUNKED;
                            nn_pipebase_received (&sws->pipebase);
                        }
                        else {
                            nn_sws_recv_hdr (sws);
                        }
                        return;

                    case NN_WS_OPCODE_CLOSE:
                        /*  If the payload is not even long enough for the
                            required 2-octet Close Code, the connection
                            should have been failed upstream. */
                        nn_assert (sws->inmsg_current_chunk_len >=
                            NN_SWS_CLOSE_CODE_LEN);
                        
                        nn_sws_validate_close_handshake (sws);
                        return;

                    default:
                        /*  This should have been prevented upstream. */
                        nn_assert (0);
                        return;
                    } 

                default:
                    nn_fsm_error ("Unexpected socket instate",
                        sws->state, src, type);
                }

            case NN_USOCK_SHUTDOWN:
                nn_pipebase_stop (&sws->pipebase);
                sws->state = NN_SWS_STATE_BROKEN_CONNECTION;
                return;

            case NN_USOCK_ERROR:
                nn_pipebase_stop (&sws->pipebase);
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_RETURN_ERROR);
                return;

            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

            break;

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  CLOSING_CONNECTION state.                                                 */
/*  Wait for acknowledgement closing handshake was successfully sent.         */
/******************************************************************************/
    case NN_SWS_STATE_CLOSING_CONNECTION:
        switch (src) {

        case NN_SWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SENT:
                /*  Wait for acknowledgement closing handshake was sent
                    to peer. */
                nn_assert (sws->outstate == NN_SWS_OUTSTATE_SENDING);
                sws->outstate = NN_SWS_OUTSTATE_IDLE;
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done,
                    NN_SWS_RETURN_CLOSE_HANDSHAKE);
                return;
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_USOCK_ERROR:
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_RETURN_ERROR);
                return;
            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  SHUTTING_DOWN state.                                                      */
/*  The underlying connection is closed. We are just waiting that underlying  */
/*  usock being closed                                                        */
/******************************************************************************/
    case NN_SWS_STATE_BROKEN_CONNECTION:
        switch (src) {

        case NN_SWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_ERROR:
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_RETURN_ERROR);
                return;
            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  DONE state.                                                               */
/*  The underlying connection is closed. There's nothing that can be done in  */
/*  this state except stopping the object.                                    */
/******************************************************************************/
    case NN_SWS_STATE_DONE:
        nn_fsm_bad_source (sws->state, src, type);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (sws->state, src, type);
    }
}

/*  Allocate a new message chunk, append it to message array, and return
    pointer to its buffer. */
static void *nn_msg_chunk_new (size_t size, struct nn_list *msg_array)
{
    struct msg_chunk *self;

    self = nn_alloc (sizeof (struct msg_chunk), "msg_chunk");
    alloc_assert (self);

    nn_chunkref_init (&self->chunk, size);
    nn_list_item_init (&self->item);

    nn_list_insert (msg_array, &self->item, nn_list_end (msg_array));

    return nn_chunkref_data (&self->chunk);
}

/*  Deallocate a message chunk and remove it from array. */
static void nn_msg_chunk_term (struct msg_chunk *it,
    struct nn_list *msg_array)
{
    nn_chunkref_term (&it->chunk);
    nn_list_erase (msg_array, &it->item);
    nn_list_item_term (&it->item);
    nn_free (it);
}

/*  Deallocate an entire message array. */
static void nn_msg_array_term (struct nn_list *msg_array)
{
    struct nn_list_item *it;
    struct msg_chunk *ch;

    while (!nn_list_empty (msg_array)) {
        it = nn_list_begin (msg_array);
        ch = nn_cont (it, struct msg_chunk, item);
        nn_msg_chunk_term (ch, msg_array);
    }

    nn_list_term (msg_array);
}

/*  Mask or unmask message payload. */
static void nn_sws_mask_payload (uint8_t *payload, size_t payload_len,
    const uint8_t *mask, size_t mask_len, int *mask_start_pos)
{
    unsigned i;

    if (mask_start_pos) {
        for (i = 0; i < payload_len; i++) {
            payload [i] ^= mask [(i + *mask_start_pos) % mask_len];
        }

        *mask_start_pos = (i + *mask_start_pos) % mask_len;

        return;
    }
    else {
        for (i = 0; i < payload_len; i++) {
            payload [i] ^= mask [i % mask_len];
        }
        return;
    }
}

/*  Start receiving new message chunk. */
static int nn_sws_recv_hdr (struct nn_sws *self)
{
    if (!self->continuing) {
        nn_assert (nn_list_empty (&self->inmsg_array));

        self->inmsg_current_chunk_buf = NULL;
        self->inmsg_chunks = 0;
        self->inmsg_current_chunk_len = 0;
        self->inmsg_total_size = 0;
    }

    memset (self->inmsg_control, 0, NN_SWS_PAYLOAD_MAX_LENGTH);
    memset (self->inhdr, 0, NN_SWS_FRAME_MAX_HDR_LEN);
    self->instate = NN_SWS_INSTATE_RECV_HDR;
    nn_usock_recv (self->usock, self->inhdr, NN_SWS_FRAME_SIZE_INITIAL, NULL);

    return 0;
}

static int nn_sws_send (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_sws *sws;
    struct nn_iovec iov [3];
    int mask_pos;
    size_t nn_msg_size;
    size_t hdr_len;
    uint8_t rand_mask [NN_SWS_FRAME_SIZE_MASK];

    sws = nn_cont (self, struct nn_sws, pipebase);

    nn_assert_state (sws, NN_SWS_STATE_ACTIVE);
    nn_assert (sws->outstate == NN_SWS_OUTSTATE_IDLE);

    /*  Move the message to the local storage. */
    nn_msg_term (&sws->outmsg);
    nn_msg_mv (&sws->outmsg, msg);

    memset (sws->outhdr, 0, sizeof (sws->outhdr));

    hdr_len = NN_SWS_FRAME_SIZE_INITIAL;

    /*  If the outgoing message has specified an opcode and control framing in
        its header, properly frame it as per RFC 6455 5.2. */
    if (nn_chunkref_size (&sws->outmsg.body) >= 1) {
        memcpy (sws->outhdr, nn_chunkref_data (&sws->outmsg.body), 1);
        nn_chunkref_trim (&sws->outmsg.body, 1);
    }
    else {
        /*  If the header does not specify an opcode, assume this default. */
        sws->outhdr [0] = NN_WS_OPCODE_BINARY | NN_SWS_FRAME_BITMASK_FIN;
    }

    nn_msg_size = nn_chunkref_size (&sws->outmsg.sphdr) +
        nn_chunkref_size (&sws->outmsg.body);

    /*  Framing WebSocket payload size in network byte order (big endian). */
    if (nn_msg_size <= NN_SWS_PAYLOAD_MAX_LENGTH) {
        sws->outhdr [1] |= (uint8_t) nn_msg_size;
        hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_0;
    }
    else if (nn_msg_size <= NN_SWS_PAYLOAD_MAX_LENGTH_16) {
        sws->outhdr [1] |= NN_SWS_PAYLOAD_FRAME_16;
        nn_puts (&sws->outhdr [hdr_len], (uint16_t) nn_msg_size);
        hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_16;
    }
    else {
        sws->outhdr [1] |= NN_SWS_PAYLOAD_FRAME_63;
        nn_putll (&sws->outhdr [hdr_len], (uint64_t) nn_msg_size);
        hdr_len += NN_SWS_FRAME_SIZE_PAYLOAD_63;
    }

    if (sws->mode == NN_WS_CLIENT) {
        sws->outhdr [1] |= NN_SWS_FRAME_BITMASK_MASKED;

        /*  Generate 32-bit mask as per RFC 6455 5.3. */
        nn_random_generate (rand_mask, NN_SWS_FRAME_SIZE_MASK);
        
        memcpy (&sws->outhdr [hdr_len], rand_mask, NN_SWS_FRAME_SIZE_MASK);
        hdr_len += NN_SWS_FRAME_SIZE_MASK;

        /*  Mask payload, beginning with header and moving to body. */
        mask_pos = 0;

        nn_sws_mask_payload (nn_chunkref_data (&sws->outmsg.sphdr),
            nn_chunkref_size (&sws->outmsg.sphdr),
            rand_mask, NN_SWS_FRAME_SIZE_MASK, &mask_pos);

        nn_sws_mask_payload (nn_chunkref_data (&sws->outmsg.body),
            nn_chunkref_size (&sws->outmsg.body),
            rand_mask, NN_SWS_FRAME_SIZE_MASK, &mask_pos);

    }
    else if (sws->mode == NN_WS_SERVER) {
        sws->outhdr [1] |= NN_SWS_FRAME_BITMASK_NOT_MASKED;
    }
    else {
        /*  Developer error; sws object was not constructed properly. */
        nn_assert (0);
    }

    /*  Start async sending. */
    iov [0].iov_base = sws->outhdr;
    iov [0].iov_len = hdr_len;
    iov [1].iov_base = nn_chunkref_data (&sws->outmsg.sphdr);
    iov [1].iov_len = nn_chunkref_size (&sws->outmsg.sphdr);
    iov [2].iov_base = nn_chunkref_data (&sws->outmsg.body);
    iov [2].iov_len = nn_chunkref_size (&sws->outmsg.body);
    nn_usock_send (sws->usock, iov, 3);

    sws->outstate = NN_SWS_OUTSTATE_SENDING;

    /*  If a Close handshake was just sent, it's time to shut down. */
    if ((sws->outhdr [0] & NN_SWS_FRAME_BITMASK_OPCODE) ==
        NN_WS_OPCODE_CLOSE) {
        nn_pipebase_stop (&sws->pipebase);
        sws->state = NN_SWS_STATE_CLOSING_CONNECTION;
    }

    return 0;
}

static int nn_sws_recv (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_sws *sws;
    struct nn_iovec iov [1];
    struct nn_list_item *it;
    struct msg_chunk *ch;
    int pos;
    size_t len;

    sws = nn_cont (self, struct nn_sws, pipebase);

    nn_assert_state (sws, NN_SWS_STATE_ACTIVE);

    switch (sws->instate) {
    case NN_SWS_INSTATE_FAILING:

        /*  Prevent further send/recv operations on this connection. */
        nn_pipebase_stop (self);
        sws->instate = NN_SWS_INSTATE_CLOSED;

        /*  Inform user this connection has been failed. */
        nn_msg_init (msg, 1);
        *(uint8_t *) nn_chunkref_data (&msg->body) = 0x7f |
            NN_SWS_FRAME_BITMASK_FIN;

        iov [0].iov_base = sws->fail_msg;
        iov [0].iov_len = sws->fail_msg_len;

        /*  TODO: Consider queueing and unconditionally sending close
            handshake rather than skipping it. */
        /*  RFC 6455 7.1.7 - try to send helpful Closing Handshake only if
            the socket is not currently sending. If it's still busy sending,
            forcibly close this connection, since it's not readily deterministic
            how much time that action could take to complete, or if the peer is
            even healthy enough to receive. Rationale: try to be nice, but be
            mindful of self-preservation! */
        if (sws->outstate == NN_SWS_OUTSTATE_IDLE) {
            nn_usock_send (sws->usock, iov, 1);
            sws->outstate = NN_SWS_OUTSTATE_SENDING;
            sws->state = NN_SWS_STATE_CLOSING_CONNECTION;
        }
        else {
            sws->state = NN_SWS_STATE_DONE;
            nn_fsm_raise (&sws->fsm, &sws->done,
                NN_SWS_RETURN_CLOSE_HANDSHAKE);
        }
        return 0;
    
    case NN_SWS_INSTATE_RECVD_CHUNKED:

        /*  This library should not deliver fragmented messages to the application,
            so it's expected that this is the final frame. */
        nn_assert (sws->is_final_frame);

        len = sws->inmsg_total_size + sizeof (sws->inmsg_hdr);

        nn_msg_init (msg, len);
            
        /*  Relay opcode, RSV and FIN bits to the user in order to
            interpret payload. */
        memcpy (nn_chunkref_data (&msg->body),
            &sws->inmsg_hdr, sizeof (sws->inmsg_hdr));
        pos = sizeof (sws->inmsg_hdr);

        /*  Reassemble incoming message scatter array. */
        while (!nn_list_empty (&sws->inmsg_array)) {
            it = nn_list_begin (&sws->inmsg_array);
            ch = nn_cont (it, struct msg_chunk, item);
            memcpy (((uint8_t*) nn_chunkref_data (&msg->body)) + pos,
                nn_chunkref_data (&ch->chunk),
                nn_chunkref_size (&ch->chunk));
            pos += nn_chunkref_size (&ch->chunk);
            nn_msg_chunk_term (ch, &sws->inmsg_array);
        }

        nn_assert (pos == len);
        nn_assert (nn_list_empty (&sws->inmsg_array));

        /*  No longer collecting scatter array of incoming msg chunks. */
        sws->continuing = 0;

        nn_sws_recv_hdr (sws);

        return 0;

    case NN_SWS_INSTATE_RECVD_CONTROL:

        /*  This library should not deliver fragmented messages to the user, so
        it's expected that this is the final frame. */
        nn_assert (sws->is_final_frame);

        len = sws->inmsg_current_chunk_len + sizeof (sws->inmsg_hdr);

        nn_msg_init (msg, len);

        /*  Relay opcode, RSV and FIN bits to the user in order to
            interpret payload. */
        memcpy (nn_chunkref_data (&msg->body),
            &sws->inhdr, sizeof (sws->inmsg_hdr));
        pos = sizeof (sws->inmsg_hdr);

        memcpy (((uint8_t*) nn_chunkref_data (&msg->body)) + pos,
            sws->inmsg_control, sws->inmsg_current_chunk_len);

        /*  If a closing handshake was just transferred to the application,
            discontinue continual, async receives. */
        if (sws->opcode == NN_WS_OPCODE_CLOSE) {
            sws->instate = NN_SWS_INSTATE_CLOSED;
        }
        else {
            nn_sws_recv_hdr (sws);
        }

        return 0;

    default:
        /*  Unexpected state. */
        nn_assert (0);
        return 0;
    }
}

/*  Ensures that Close frames received from peer conform to
    RFC 6455 section 7. */
static void nn_sws_validate_close_handshake (struct nn_sws *self)
{
    uint16_t close_code;

    /*  TODO: As per RFC 6455 7.1.6, the Close Reason following the Close Code
        must be well-formed UTF-8. Can we be liberal here (as per Postel
        principle) and not check the validity of the UTF-8 here? */

    close_code = nn_gets (self->inmsg_current_chunk_buf);

    if (close_code == NN_SWS_CLOSE_NORMAL ||
        close_code == NN_SWS_CLOSE_GOING_AWAY ||
        close_code == NN_SWS_CLOSE_ERR_PROTO ||
        close_code == NN_SWS_CLOSE_ERR_WUT ||
        close_code == NN_SWS_CLOSE_ERR_INVALID_FRAME ||
        close_code == NN_SWS_CLOSE_ERR_POLICY ||
        close_code == NN_SWS_CLOSE_ERR_TOOBIG ||
        close_code == NN_SWS_CLOSE_ERR_EXTENSION ||
        close_code == NN_SWS_CLOSE_ERR_SERVER) {
        /*  RFC 6455 7.4.1 */
        self->instate = NN_SWS_INSTATE_RECVD_CONTROL;
        nn_pipebase_received (&self->pipebase);
    }
    else if (close_code >= 3000 && close_code <= 3999) {
        /*  RFC 6455 7.4.2 */
        self->instate = NN_SWS_INSTATE_RECVD_CONTROL;
        nn_pipebase_received (&self->pipebase);
    }
    else if (close_code >= 4000 && close_code <= 4999) {
        /*  RFC 6455 7.4.2 */
        self->instate = NN_SWS_INSTATE_RECVD_CONTROL;
        nn_pipebase_received (&self->pipebase);
    }
    else {
        nn_sws_fail_conn (self, NN_SWS_CLOSE_ERR_PROTO,
            "Unrecognized close code.");
    }
}

/*  Ceases further I/O on the underlying socket and prepares to send a
    close handshake on the next receive. */
static void nn_sws_fail_conn (struct nn_sws *self, int code, char *reason)
{
    size_t reason_len;
    size_t payload_len;
    uint8_t rand_mask [NN_SWS_FRAME_SIZE_MASK];
    uint8_t *payload_pos;

    nn_assert_state (self, NN_SWS_STATE_ACTIVE);

    /*  Destroy any remnant incoming message fragments. */
    nn_msg_array_term (&self->inmsg_array);

    reason_len = strlen (reason);

    payload_len = reason_len + NN_SWS_CLOSE_CODE_LEN;

    /*  Ensure text is short enough to also include code and framing. */
    nn_assert (payload_len <= NN_SWS_PAYLOAD_MAX_LENGTH);

    /*  RFC 6455 section 5.5.1. */
    self->fail_msg [0] = NN_SWS_FRAME_BITMASK_FIN | NN_WS_OPCODE_CLOSE;

    /*  Size of the payload, which is the status code plus the reason. */
    self->fail_msg [1] = payload_len;

    self->fail_msg_len = NN_SWS_FRAME_SIZE_INITIAL;

    if (self->mode == NN_WS_SERVER) {
        self->fail_msg [1] |= NN_SWS_FRAME_BITMASK_NOT_MASKED;
    }
    else if (self->mode == NN_WS_CLIENT) {
        self->fail_msg [1] |= NN_SWS_FRAME_BITMASK_MASKED;

        /*  Generate 32-bit mask as per RFC 6455 5.3. */
        nn_random_generate (rand_mask, NN_SWS_FRAME_SIZE_MASK);
        
        memcpy (&self->fail_msg [NN_SWS_FRAME_SIZE_INITIAL],
            rand_mask, NN_SWS_FRAME_SIZE_MASK);

        self->fail_msg_len += NN_SWS_FRAME_SIZE_MASK;
    }
    else {
        /*  Developer error. */
        nn_assert (0);
    }

    payload_pos = &self->fail_msg [self->fail_msg_len];
    
    /*  Copy Status Code in network order (big-endian). */
    nn_puts (payload_pos, (uint16_t) code);
    self->fail_msg_len += NN_SWS_CLOSE_CODE_LEN;

    /*  Copy Close Reason immediately following the code. */
    memcpy (payload_pos + NN_SWS_CLOSE_CODE_LEN, reason, reason_len);

    /*  If this is a client, apply mask. */
    if (self->mode == NN_WS_CLIENT) {
        nn_sws_mask_payload (payload_pos, payload_len,
            rand_mask, NN_SWS_FRAME_SIZE_MASK, NULL);
    }

    self->fail_msg_len += payload_len;

    self->instate = NN_SWS_INSTATE_FAILING;

    /*  On the next recv, the connection will be failed. Why defer
        until the next recv? Semantically, until then, this incoming
        message has not been interpreted, so it's not until then that
        it could be failed. This type of pre-processing is necessary
        to early fail chunked transfers. */
    nn_pipebase_received (&self->pipebase);
}

