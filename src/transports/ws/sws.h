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

#ifndef NN_SWS_INCLUDED
#define NN_SWS_INCLUDED

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/msg.h"
#include "../../utils/list.h"

#include "masker.h"

/*  This state machine handles WS connection from the point where it is
    established to the point when it is broken. */

#define NN_SWS_MODE_SERVER 1
#define NN_SWS_MODE_CLIENT 2

#define NN_SWS_ERROR 1
#define NN_SWS_STOPPED 2

struct nn_sws {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  Either NN_SWS_MODE_SERVER or NN_SWS_MODE_CLIENT. */
    int mode;

    /*  The underlying socket. */
    struct nn_usock *usock;

    /*  The original owner of the underlying socket. */
    struct nn_fsm_owner usock_owner;

    /*  Pipe connecting this WS connection to the nanomsg core. */
    struct nn_pipebase pipebase;

    /*  State of inbound state machine. */
    int instate;

    /*  Buffer used to store the header of incoming message. The longest
        possible header is 14 bytes long, see RFC 6455, section 5.2. */
    uint8_t inhdr [14];

    /*  Message being received at the moment, if form of list of fragments. */
    struct nn_list inmsg;

    /*  Total size of all the fragments inside inmsg list. */
    size_t inmsglen;

    /*  State of the outbound state machine. */
    int outstate;

    /*  Buffer used to store the header of outgoing message. The longest
        possible header is 14 bytes long, see RFC 6455, section 5.2. */
    uint8_t outhdr [14];

    /*  Message being sent at the moment. */
    struct nn_msg outmsg;

    /*  We need only one instance of masker as it is used either for encoding
        (on client) or decoding (on server), never both. */
    struct nn_masker masker;

    /*  Event raised when the state machine ends. */
    struct nn_fsm_event done;
};

void nn_sws_init (struct nn_sws *self, int src,
    struct nn_epbase *epbase, struct nn_fsm *owner);
void nn_sws_term (struct nn_sws *self);

int nn_sws_isidle (struct nn_sws *self);
void nn_sws_start (struct nn_sws *self, struct nn_usock *usock, int mode);
void nn_sws_stop (struct nn_sws *self);

#endif

