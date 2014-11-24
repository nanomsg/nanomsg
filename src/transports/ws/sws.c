/*
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.
    Copyright (c) 2014 Martin Sustrik  All rights reserved.

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

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/wire.h"
#include "../../utils/int.h"
#include "../../utils/attr.h"
#include "../../utils/random.h"

/*  States of the object as a whole. */
#define NN_SWS_STATE_IDLE 1
#define NN_SWS_STATE_PROTOHDR 2
#define NN_SWS_STATE_STOPPING_STREAMHDR 3
#define NN_SWS_STATE_ACTIVE 4
#define NN_SWS_STATE_SHUTTING_DOWN 5
#define NN_SWS_STATE_DONE 6
#define NN_SWS_STATE_STOPPING 7

/*  Possible states of the inbound part of the object. */
#define NN_SWS_INSTATE_HDR 1
#define NN_SWS_INSTATE_HDR_EXT 2
#define NN_SWS_INSTATE_BODY 3
#define NN_SWS_INSTATE_HASMSG 4

/*  Possible states of the outbound part of the object. */
#define NN_SWS_OUTSTATE_IDLE 1
#define NN_SWS_OUTSTATE_SENDING 2

/*  Subordinate srcptr objects. */
#define NN_SWS_SRC_USOCK 1
#define NN_SWS_SRC_STREAMHDR 2

/*  Constants to compose first byte of WebSocket message header from. */
#define NN_SWS_FIN 0x80
#define NN_SWS_RSVS 0x70
#define NN_SWS_OPCODE 0x0f
#define NN_SWS_OPCODE_CONTINUATION 0x00
#define NN_SWS_OPCODE_BINARY 0x02
#define NN_SWS_OPCODE_CLOSE 0x08

/*  Constants for the second byte of WebSocket message header. */
#define NN_SWS_MASK 0x80
#define NN_SWS_SIZE 0x7f
#define NN_SWS_SIZE_16 0x7e
#define NN_SWS_SIZE_64 0x7f

/*  Stream is a special type of pipe. Implementation of the virtual pipe API. */
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

void nn_sws_init (struct nn_sws *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_sws_handler, nn_sws_shutdown,
        src, self, owner);
    self->state = NN_SWS_STATE_IDLE;
    nn_streamhdr_init (&self->streamhdr, NN_SWS_SRC_STREAMHDR, &self->fsm);
    self->usock = NULL;
    self->usock_owner.src = -1;
    self->usock_owner.fsm = NULL;
    nn_pipebase_init (&self->pipebase, &nn_sws_pipebase_vfptr, epbase);
    self->instate = -1;
    nn_msg_init (&self->inmsg, 0);
    self->outstate = -1;
    nn_msg_init (&self->outmsg, 0);
    nn_fsm_event_init (&self->done);
}

void nn_sws_term (struct nn_sws *self)
{
    nn_assert_state (self, NN_SWS_STATE_IDLE);

    nn_fsm_event_term (&self->done);
    nn_msg_term (&self->outmsg);
    nn_msg_term (&self->inmsg);
    nn_pipebase_term (&self->pipebase);
    nn_streamhdr_term (&self->streamhdr);
    nn_fsm_term (&self->fsm);
}

int nn_sws_isidle (struct nn_sws *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_sws_start (struct nn_sws *self, struct nn_usock *usock, int mode)
{
    /*  There are only two valid modes. */
    nn_assert (mode == NN_SWS_MODE_SERVER || mode == NN_SWS_MODE_CLIENT);
    self->mode = mode;

    /*  Take ownership of the underlying socket. */
    nn_assert (self->usock == NULL && self->usock_owner.fsm == NULL);
    self->usock_owner.src = NN_SWS_SRC_USOCK;
    self->usock_owner.fsm = &self->fsm;
    nn_usock_swap_owner (usock, &self->usock_owner);
    self->usock = usock;

    /*  Launch the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_sws_stop (struct nn_sws *self)
{
    nn_fsm_stop (&self->fsm);
}

static int nn_sws_send (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_sws *sws;
    size_t sz;
    size_t hdrsz;
    struct nn_iovec iov [3];

    sws = nn_cont (self, struct nn_sws, pipebase);

    nn_assert_state (sws, NN_SWS_STATE_ACTIVE);
    nn_assert (sws->outstate == NN_SWS_OUTSTATE_IDLE);

    /*  Move the message to the local storage. */
    nn_msg_term (&sws->outmsg);
    nn_msg_mv (&sws->outmsg, msg);

    /*  Serialise the message header. */
    sws->outhdr [0] = NN_SWS_FIN | NN_SWS_OPCODE_BINARY;
    hdrsz = 1;
    
    /*  Frame the payload size. Don't set the mask bit yet. */
    sz = nn_chunkref_size (&sws->outmsg.sphdr) +
        nn_chunkref_size (&sws->outmsg.body);
    if (sz <= 0x7d) {
        sws->outhdr [1] = (uint8_t) sz;
        hdrsz += 1;
    }
    else if (sz <= 0xffff) {
        sws->outhdr [1] = 0x7e;
        nn_puts (&sws->outhdr [2], (uint16_t) sz);
        hdrsz += 3;
    }
    else {
        sws->outhdr [1] = 0x7f;
        nn_putll (&sws->outhdr [2], (uint64_t) sz);
        hdrsz += 9;
    }

    /*  When sending from client to server, mask the message. */
    /*  TODO: Consider that nn_random doesn't produce cryptographically strong
               random data. But then: Can we produce enough random data at high
               message rates without running out of available entropy? */
    if (sws->mode == NN_SWS_MODE_CLIENT) {
        sws->outhdr [1] |= NN_SWS_MASK;
        nn_random_generate (&sws->outhdr [hdrsz], 4);
        nn_masker_init (&sws->masker, &sws->outhdr [hdrsz]);
        hdrsz += 4;

        /* TODO: If the message is shared, this is going to break
                  everything... */
        nn_masker_mask (&sws->masker, nn_chunkref_data (&sws->outmsg.sphdr),
            nn_chunkref_size (&sws->outmsg.sphdr));
        nn_masker_mask (&sws->masker, nn_chunkref_data (&sws->outmsg.body),
            nn_chunkref_size (&sws->outmsg.body));
    }

    /*  Start async sending. */
    iov [0].iov_base = sws->outhdr;
    iov [0].iov_len = hdrsz;
    iov [1].iov_base = nn_chunkref_data (&sws->outmsg.sphdr);
    iov [1].iov_len = nn_chunkref_size (&sws->outmsg.sphdr);
    iov [2].iov_base = nn_chunkref_data (&sws->outmsg.body);
    iov [2].iov_len = nn_chunkref_size (&sws->outmsg.body);
    nn_usock_send (sws->usock, iov, 3);

    sws->outstate = NN_SWS_OUTSTATE_SENDING;

    return 0;
}

static int nn_sws_recv (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_sws *sws;

    sws = nn_cont (self, struct nn_sws, pipebase);

    nn_assert_state (sws, NN_SWS_STATE_ACTIVE);
    nn_assert (sws->instate == NN_SWS_INSTATE_HASMSG);

    /*  Move received message to the user. */
    nn_msg_mv (msg, &sws->inmsg);
    nn_msg_init (&sws->inmsg, 0);

    /*  Start receiving new message by reading 2 bytes. That's a minimal
        message header and by looking at it we'll find out whether any
        additional bytes have to be read. */
    sws->instate = NN_SWS_INSTATE_HDR;
    nn_usock_recv (sws->usock, sws->inhdr, 2, NULL);

    return 0;
}

static void nn_sws_shutdown (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_sws *sws;

    sws = nn_cont (self, struct nn_sws, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        nn_pipebase_stop (&sws->pipebase);
        nn_streamhdr_stop (&sws->streamhdr);
        sws->state = NN_SWS_STATE_STOPPING;
    }
    if (nn_slow (sws->state == NN_SWS_STATE_STOPPING)) {
        if (nn_streamhdr_isidle (&sws->streamhdr)) {
            nn_usock_swap_owner (sws->usock, &sws->usock_owner);
            sws->usock = NULL;
            sws->usock_owner.src = -1;
            sws->usock_owner.fsm = NULL;
            sws->state = NN_SWS_STATE_IDLE;
            nn_fsm_stopped (&sws->fsm, NN_SWS_STOPPED);
            return;
        }
        return;
    }

    nn_fsm_bad_state(sws->state, src, type);
}

static void nn_sws_handler (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    int rc;
    struct nn_sws *sws;
    uint64_t size;

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
                nn_streamhdr_start (&sws->streamhdr, sws->usock,
                    &sws->pipebase);
                sws->state = NN_SWS_STATE_PROTOHDR;
                return;
            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  PROTOHDR state.                                                           */
/******************************************************************************/
    case NN_SWS_STATE_PROTOHDR:
        switch (src) {

        case NN_SWS_SRC_STREAMHDR:
            switch (type) {
            case NN_STREAMHDR_OK:

                /*  Before moving to the active state stop the streamhdr
                    state machine. */
                nn_streamhdr_stop (&sws->streamhdr);
                sws->state = NN_SWS_STATE_STOPPING_STREAMHDR;
                return;

            case NN_STREAMHDR_ERROR:

                /* Raise the error and move directly to the DONE state.
                   streamhdr object will be stopped later on. */
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_ERROR);
                return;

            default:
                nn_fsm_bad_action (sws->state, src, type);
            }

        default:
            nn_fsm_bad_source (sws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_STREAMHDR state.                                                 */
/******************************************************************************/
    case NN_SWS_STATE_STOPPING_STREAMHDR:
        switch (src) {

        case NN_SWS_SRC_STREAMHDR:
            switch (type) {
            case NN_STREAMHDR_STOPPED:

                 /*  Start the pipe. */
                 rc = nn_pipebase_start (&sws->pipebase);
                 if (nn_slow (rc < 0)) {
                    sws->state = NN_SWS_STATE_DONE;
                    nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_ERROR);
                    return;
                 }

                 /*  Start receiving a message in asynchronous manner. */
                 sws->instate = NN_SWS_INSTATE_HDR;
                 nn_usock_recv (sws->usock, &sws->inhdr, 2, NULL);

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
                case NN_SWS_INSTATE_HDR:

                    /* TODO */
                    nn_assert ((sws->inhdr [0] & NN_SWS_FIN) != 0);

                    /*  Reserved bits should not be set. */
                    nn_assert ((sws->inhdr [0] & NN_SWS_RSVS) == 0);

                    /*  WS mapping for SP accepts only binary messages. */
                    nn_assert ((sws->inhdr [0] & NN_SWS_OPCODE) ==
                        NN_SWS_OPCODE_BINARY);

                    /*  Server accepts only masked messages,
                        client accepts only unmasked messages. */
                    if (sws->mode == NN_SWS_MODE_SERVER)
                        nn_assert ((sws->inhdr [1] & NN_SWS_MASK) != 0);
                    else
                        nn_assert ((sws->inhdr [1] & NN_SWS_MASK) == 0);

                    /*  Find out how many additional bytes we have to read
                        to get the entire message header. */
                    size = 0;
                    if (sws->inhdr [1] & NN_SWS_MASK)
                        size += 4;
                    if ((sws->inhdr [1] & NN_SWS_SIZE) == NN_SWS_SIZE_16)
                        size += 2;
                    else if ((sws->inhdr [1] & NN_SWS_SIZE) == NN_SWS_SIZE_64)
                        size += 8;

                    /*  Get the additional bytes. */
                    sws->instate = NN_SWS_INSTATE_HDR_EXT;
                    if (size > 0) {
                        nn_usock_recv (sws->usock, &sws->inhdr [2],
                           (size_t) size, NULL);
                        return;
                    }

                    /*  If there are no additional bytes to read fall through
                        to the next state. */

                case NN_SWS_INSTATE_HDR_EXT:

                    /*  Message header was fully received.
                        Now determine the payload size. */
                    if ((sws->inhdr [1] & NN_SWS_SIZE) == NN_SWS_SIZE_16) {
                        size = nn_gets (&sws->inhdr [2]);
                        if (sws->inhdr [1] & NN_SWS_MASK)
                            nn_masker_init (&sws->masker, &sws->inhdr [4]);
                    }
                    else if ((sws->inhdr[1] & NN_SWS_SIZE) == NN_SWS_SIZE_64) {
                        size = nn_getll (&sws->inhdr [2]);
                        if (sws->inhdr [1] & NN_SWS_MASK)
                            nn_masker_init (&sws->masker, &sws->inhdr [10]);
                    }
                    else {
                        size = sws->inhdr [1] & NN_SWS_SIZE;
                        if (sws->inhdr [1] & NN_SWS_MASK)
                            nn_masker_init (&sws->masker, &sws->inhdr [2]);
                    }
 
                    /* Allocate memory for the message. */
                    nn_msg_term (&sws->inmsg);
                    nn_msg_init (&sws->inmsg, (size_t) size);

                    /*  Special case when size of the message body is 0. */
                    if (!size) {
                        sws->instate = NN_SWS_INSTATE_HASMSG;
                        nn_pipebase_received (&sws->pipebase);
                        return;
                    }

                    /*  Start receiving the message body. */
                    sws->instate = NN_SWS_INSTATE_BODY;
                    nn_usock_recv (sws->usock,
                        nn_chunkref_data (&sws->inmsg.body),
                       (size_t) size, NULL);

                    return;

                case NN_SWS_INSTATE_BODY:

                    /*  Unmask the message body, if needed. */
                    if (sws->inhdr [1] & NN_SWS_MASK) {
                        nn_masker_mask (&sws->masker,
                            nn_chunkref_data (&sws->inmsg.body),
                            nn_chunkref_size (&sws->inmsg.body));
                    }

                    /*  Message body is now fully received.
                        Notify the owner that it can receive it. */
                    sws->instate = NN_SWS_INSTATE_HASMSG;
                    nn_pipebase_received (&sws->pipebase);

                    return;

                default:
                    nn_fsm_error("Unexpected socket instate",
                        sws->state, src, type);
                }

            case NN_USOCK_SHUTDOWN:
                nn_pipebase_stop (&sws->pipebase);
                sws->state = NN_SWS_STATE_SHUTTING_DOWN;
                return;

            case NN_USOCK_ERROR:
                nn_pipebase_stop (&sws->pipebase);
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_ERROR);
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
    case NN_SWS_STATE_SHUTTING_DOWN:
        switch (src) {

        case NN_SWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_ERROR:
                sws->state = NN_SWS_STATE_DONE;
                nn_fsm_raise (&sws->fsm, &sws->done, NN_SWS_ERROR);
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

