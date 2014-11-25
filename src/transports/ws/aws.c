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

#include "aws.h"
#include "sha1.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/attr.h"
#include "../../utils/alloc.h"
#include "../../utils/wire.h"

#include "../utils/base64.h"

#define NN_AWS_STATE_IDLE 1
#define NN_AWS_STATE_ACCEPTING 2
#define NN_AWS_STATE_RECEIVING_WSHDR 3
#define NN_AWS_STATE_SENDING_HDR 4
#define NN_AWS_STATE_RECEIVING_SPHDR 5
#define NN_AWS_STATE_ACTIVE 6
#define NN_AWS_STATE_STOPPING_SWS 7
#define NN_AWS_STATE_STOPPING_USOCK 8
#define NN_AWS_STATE_DONE 9
#define NN_AWS_STATE_STOPPING_SWS_FINAL 10
#define NN_AWS_STATE_STOPPING 11

#define NN_AWS_SRC_USOCK 1
#define NN_AWS_SRC_SWS 2
#define NN_AWS_SRC_LISTENER 3

#define NN_AWS_BUF_SIZE 2048

/*  Private functions. */
static void nn_aws_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_aws_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_aws_convert_key (const char *buf, size_t len,
    char *result, size_t result_len);

void nn_aws_init (struct nn_aws *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_aws_handler, nn_aws_shutdown,
        src, self, owner);
    self->state = NN_AWS_STATE_IDLE;
    self->epbase = epbase;
    nn_usock_init (&self->usock, NN_AWS_SRC_USOCK, &self->fsm);
    self->listener = NULL;
    self->listener_owner.src = -1;
    self->listener_owner.fsm = NULL;
    nn_sws_init (&self->sws, NN_AWS_SRC_SWS, epbase, &self->fsm);
    self->buf = NULL;
    nn_fsm_event_init (&self->accepted);
    nn_fsm_event_init (&self->done);
    nn_list_item_init (&self->item);
}

void nn_aws_term (struct nn_aws *self)
{
    nn_assert_state (self, NN_AWS_STATE_IDLE);

    nn_list_item_term (&self->item);
    nn_fsm_event_term (&self->done);
    nn_fsm_event_term (&self->accepted);
    nn_sws_term (&self->sws);
    if (self->buf)
        nn_free (self->buf);
    nn_usock_term (&self->usock);
    nn_fsm_term (&self->fsm);
}

int nn_aws_isidle (struct nn_aws *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_aws_start (struct nn_aws *self, struct nn_usock *listener)
{
    nn_assert_state (self, NN_AWS_STATE_IDLE);

    /*  Take ownership of the listener socket. */
    self->listener = listener;
    self->listener_owner.src = NN_AWS_SRC_LISTENER;
    self->listener_owner.fsm = &self->fsm;
    nn_usock_swap_owner (listener, &self->listener_owner);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);
}

void nn_aws_stop (struct nn_aws *self)
{
    nn_fsm_stop (&self->fsm);
}

static void nn_aws_shutdown (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_aws *aws;

    aws = nn_cont (self, struct nn_aws, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        if (!nn_sws_isidle (&aws->sws)) {
            nn_epbase_stat_increment (aws->epbase,
                NN_STAT_DROPPED_CONNECTIONS, 1);
            nn_sws_stop (&aws->sws);
        }
        aws->state = NN_AWS_STATE_STOPPING_SWS_FINAL;
    }
    if (nn_slow (aws->state == NN_AWS_STATE_STOPPING_SWS_FINAL)) {
        if (!nn_sws_isidle (&aws->sws))
            return;
        nn_usock_stop (&aws->usock);
        aws->state = NN_AWS_STATE_STOPPING;
    }
    if (nn_slow (aws->state == NN_AWS_STATE_STOPPING)) {
        if (!nn_usock_isidle (&aws->usock))
            return;
       if (aws->listener) {
            nn_assert (aws->listener_owner.fsm);
            nn_usock_swap_owner (aws->listener, &aws->listener_owner);
            aws->listener = NULL;
            aws->listener_owner.src = -1;
            aws->listener_owner.fsm = NULL;
        }
        aws->state = NN_AWS_STATE_IDLE;
        nn_fsm_stopped (&aws->fsm, NN_AWS_STOPPED);
        return;
    }

    nn_fsm_bad_action(aws->state, src, type);
}

static void nn_aws_handler (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_aws *aws;
    struct nn_iovec iov;
    int val;
    size_t sz;
    char key [29];

    aws = nn_cont (self, struct nn_aws, fsm);

    switch (aws->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_AWS_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_usock_accept (&aws->usock, aws->listener);
                aws->state = NN_AWS_STATE_ACCEPTING;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  ACCEPTING state.                                                          */
/*  Waiting for incoming connection.                                          */
/******************************************************************************/
    case NN_AWS_STATE_ACCEPTING:
        switch (src) {

        case NN_AWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_ACCEPTED:
                nn_epbase_clear_error (aws->epbase);

                /*  Set the relevant socket options. */
                sz = sizeof (val);
                nn_epbase_getopt (aws->epbase, NN_SOL_SOCKET, NN_SNDBUF,
                    &val, &sz);
                nn_assert (sz == sizeof (val));
                nn_usock_setsockopt (&aws->usock, SOL_SOCKET, SO_SNDBUF,
                    &val, sizeof (val));
                sz = sizeof (val);
                nn_epbase_getopt (aws->epbase, NN_SOL_SOCKET, NN_RCVBUF,
                    &val, &sz);
                nn_assert (sz == sizeof (val));
                nn_usock_setsockopt (&aws->usock, SOL_SOCKET, SO_RCVBUF,
                    &val, sizeof (val));

                /*  Return ownership of the listening socket to the parent. */
                nn_usock_swap_owner (aws->listener, &aws->listener_owner);
                aws->listener = NULL;
                aws->listener_owner.src = -1;
                aws->listener_owner.fsm = NULL;
                nn_fsm_raise (&aws->fsm, &aws->accepted, NN_AWS_ACCEPTED);

                /*  Allocate the buffer to be used during the handshake. */
                nn_assert (aws->buf == NULL);
                aws->buf = nn_alloc (NN_AWS_BUF_SIZE, "aws handshake buffer");
                alloc_assert (aws->buf);

                /*  Start reading the request. It is at least 150 bytes long. */
                nn_usock_activate (&aws->usock);
                nn_assert (NN_AWS_BUF_SIZE >= 150);
                nn_usock_recv (&aws->usock, aws->buf, 150, NULL);
                aws->bufsz = 150;
                aws->state = NN_AWS_STATE_RECEIVING_WSHDR;

                return;

            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        case NN_AWS_SRC_LISTENER:
            switch (type) {

            case NN_USOCK_ACCEPT_ERROR:
                nn_epbase_set_error (aws->epbase,
                    nn_usock_geterrno(aws->listener));
                nn_epbase_stat_increment (aws->epbase,
                    NN_STAT_ACCEPT_ERRORS, 1);
                nn_usock_accept (&aws->usock, aws->listener);
                return;

            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  RECEIVING_WSHDR state.                                                    */
/*  WebSocket connection request from the client is being read.               */
/******************************************************************************/
    case NN_AWS_STATE_RECEIVING_WSHDR:
        switch (src) {

        case NN_AWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_RECEIVED:

                /*  Check whether WebSocket connection reply was fully read.
                    If not so, read one more byte and repeat. */
                nn_assert (aws->bufsz >= 4);
                if (memcmp (&aws->buf [aws->bufsz - 4], "\r\n\r\n", 4) != 0) {
                    nn_assert (NN_AWS_BUF_SIZE >= aws->bufsz + 1);
                    nn_usock_recv (&aws->usock,
                        &aws->buf [aws->bufsz], 1, NULL);
                    ++aws->bufsz;
                    return;
                }

                /*  TODO: Validate the request. */

                /*  Send the WebSocket connection reply. */
                nn_aws_convert_key ("0123456789012345678901234567", 28,
                    key, sizeof (key)); // TODO
                iov.iov_base = aws->buf;
                iov.iov_len = snprintf (aws->buf, NN_AWS_BUF_SIZE,
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: %s\r\n"
                        "Sec-WebSocket-Protocol: sp\r\n\r\n",
                    key);
                nn_assert (iov.iov_len + 10 <= NN_AWS_BUF_SIZE);

                /*  Bundle SP header with the request. */
                sz = sizeof (val);
                nn_epbase_getopt (aws->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
                    &val, &sz);
                nn_assert (sz == sizeof (val));
                aws->buf [iov.iov_len] = 0x82;
                aws->buf [iov.iov_len + 1] = 0x08;
                aws->buf [iov.iov_len + 2] = 0x00;
                aws->buf [iov.iov_len + 3] = 'S';
                aws->buf [iov.iov_len + 4] = 'P';
                aws->buf [iov.iov_len + 5] = 0x00;
                nn_puts (&aws->buf [iov.iov_len + 6], val);
                aws->buf [iov.iov_len + 8] = 0x00;
                aws->buf [iov.iov_len + 9] = 0x00;
                iov.iov_len += 10;

                /*  Send it to the peer. */
                nn_usock_send (&aws->usock, &iov, 1);
                aws->state = NN_AWS_STATE_SENDING_HDR;

                return;

            case NN_USOCK_ERROR:
                nn_usock_stop (&aws->usock);
                aws->state = NN_AWS_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  SENDING_HDR state.                                                        */
/*  WebSocket connection reply is being sent along with SP protocol header.   */
/******************************************************************************/
    case NN_AWS_STATE_SENDING_HDR:
        switch (src) {

        case NN_AWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SENT:

                /*  Reply is sent. Now read the SP protocol header. */
                nn_assert (NN_AWS_BUF_SIZE >= 14);
                nn_usock_recv (&aws->usock, aws->buf, 14, NULL);
                aws->bufsz = 14;
                aws->state = NN_AWS_STATE_RECEIVING_SPHDR;
                return;

            case NN_USOCK_ERROR:
                nn_usock_stop (&aws->usock);
                aws->state = NN_AWS_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  RECEIVING_SPHDR state.                                                    */
/*  SP protocol header is being read.                                         */
/******************************************************************************/
    case NN_AWS_STATE_RECEIVING_SPHDR:
        switch (src) {

        case NN_AWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_RECEIVED:

                /*  TODO: Validate the SP header. */
                
                /*  Start the sws state machine. */
                nn_free (aws->buf);
                aws->buf = NULL;
                nn_sws_start (&aws->sws, &aws->usock, NN_SWS_MODE_SERVER);
                aws->state = NN_AWS_STATE_ACTIVE;                
                nn_epbase_stat_increment (aws->epbase,
                    NN_STAT_ACCEPTED_CONNECTIONS, 1);
                return;
            case NN_USOCK_ERROR:
                nn_usock_stop (&aws->usock);
                aws->state = NN_AWS_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
    case NN_AWS_STATE_ACTIVE:
        switch (src) {

        case NN_AWS_SRC_SWS:
            switch (type) {
            case NN_SWS_ERROR:
                nn_sws_stop (&aws->sws);
                aws->state = NN_AWS_STATE_STOPPING_SWS;
                nn_epbase_stat_increment (aws->epbase,
                    NN_STAT_BROKEN_CONNECTIONS, 1);
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_SWS state.                                                      */
/******************************************************************************/
    case NN_AWS_STATE_STOPPING_SWS:
        switch (src) {

        case NN_AWS_SRC_SWS:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_SWS_STOPPED:
                nn_usock_stop (&aws->usock);
                aws->state = NN_AWS_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_USOCK state.                                                      */
/******************************************************************************/
    case NN_AWS_STATE_STOPPING_USOCK:
        switch (src) {

        case NN_AWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_USOCK_STOPPED:
                nn_fsm_raise (&aws->fsm, &aws->done, NN_AWS_ERROR);
                aws->state = NN_AWS_STATE_DONE;
                return;
            default:
                nn_fsm_bad_action (aws->state, src, type);
            }

        default:
            nn_fsm_bad_source (aws->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (aws->state, src, type);
    }
}

static void nn_aws_convert_key (const char *buf, size_t len,
    char *result, size_t result_len)
{
    int rc;
    struct nn_sha1 sha1;
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    nn_assert (len >= 28);

    nn_sha1_init (&sha1);
    nn_sha1_hash (&sha1, buf, 28);
    nn_sha1_hash (&sha1, magic, 36);
    rc = nn_base64_encode (nn_sha1_result (&sha1),
        NN_SHA1_RESULT_LEN, result, result_len);
    errnum_assert (rc >= 0, -rc);
    nn_assert (rc == 28);
}

