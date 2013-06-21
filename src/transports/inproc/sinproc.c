/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "sinproc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

#include <stddef.h>

#define NN_SINPROC_STATE_IDLE 1
#define NN_SINPROC_STATE_CONNECTING 2
#define NN_SINPROC_STATE_ACTIVE 3

#define NN_SINPROC_ACTION_ACCEPTED 1

/*  Set when SENT event was sent to the peer but RECEIVED haven't been
    passed back yet. */
#define NN_SINPROC_FLAG_SENDING 1

/*  Set when SENT event was received, but the new message cannot be written
    to the queue yet, i.e. RECEIVED event haven't been returned
    to the peer yet. */
#define NN_SINPROC_FLAG_RECEIVING 2

/*  Private functions. */
void nn_sinproc_handler (struct nn_fsm *self, void *source, int type);

static int nn_sinproc_send (struct nn_pipebase *self, struct nn_msg *msg);
static int nn_sinproc_recv (struct nn_pipebase *self, struct nn_msg *msg);
const struct nn_pipebase_vfptr nn_sinproc_pipebase_vfptr = {
    nn_sinproc_send,
    nn_sinproc_recv
};

void nn_sinproc_init (struct nn_sinproc *self, struct nn_epbase *epbase,
    struct nn_fsm *owner)
{
printf ("%p init\n", self);
    nn_fsm_init (&self->fsm, nn_sinproc_handler, owner);
    self->state = NN_SINPROC_STATE_IDLE;
    self->flags = 0;
    self->peer = NULL;
    nn_pipebase_init (&self->pipebase, &nn_sinproc_pipebase_vfptr, epbase);
    nn_msgqueue_init (&self->msgqueue, 1000000);
    nn_msg_init (&self->msg, 0);
    nn_fsm_event_init (&self->event_connect);
    nn_fsm_event_init (&self->event_sent);
    nn_fsm_event_init (&self->event_received);
    nn_list_item_init (&self->item);
}

void nn_sinproc_term (struct nn_sinproc *self)
{
printf ("%p term\n", self);
    nn_list_item_term (&self->item);
    nn_fsm_event_term (&self->event_received);
    nn_fsm_event_term (&self->event_sent);
    nn_fsm_event_term (&self->event_connect);
    nn_msg_term (&self->msg);
    nn_msgqueue_term (&self->msgqueue);
    nn_pipebase_term (&self->pipebase);
    nn_fsm_term (&self->fsm);
}

void nn_sinproc_start (struct nn_sinproc *self, struct nn_fsm *peer)
{
printf ("%p start\n", self);
    nn_fsm_start (&self->fsm);

    /*  Start the connecting handshake with the peer. */
printf ("%p sending CONNECT\n", self);
    nn_fsm_raiseto (&self->fsm, peer, &self->event_connect, self,
        NN_SINPROC_CONNECT);
}

void nn_sinproc_start_accept (struct nn_sinproc *self, struct nn_sinproc *peer)
{
    int rc;

printf ("%p start_peer\n", self);
    nn_assert (!self->peer);
    self->peer = peer;
    

    /*  Start the connecting handshake with the peer. */
printf ("%p sending ACCEPTED\n", self);
    nn_fsm_raiseto (&self->fsm, &peer->fsm, &self->event_connect, self,
        NN_SINPROC_ACCEPTED);

    /*  Notify the state machine. */
    nn_fsm_start (&self->fsm);
    nn_sinproc_handler (&self->fsm, NULL, NN_SINPROC_ACTION_ACCEPTED);
}

void nn_sinproc_stop (struct nn_sinproc *self)
{
printf ("%p stop\n", self);
    nn_fsm_stop (&self->fsm);
}

static int nn_sinproc_send (struct nn_pipebase *self, struct nn_msg *msg)
{
    struct nn_sinproc *sinproc;

    sinproc = nn_cont (self, struct nn_sinproc, pipebase);

    /*  Sanity checks. */
    nn_assert (sinproc->state == NN_SINPROC_STATE_ACTIVE);
    nn_assert (!(sinproc->flags & NN_SINPROC_FLAG_SENDING));

    /*  Expose the message to the peer. */
    nn_msg_term (&sinproc->msg);
    nn_msg_mv (&sinproc->msg, msg);

    /*  Notify the peer that there's a message to get. */
    sinproc->flags |= NN_SINPROC_FLAG_SENDING;
    nn_fsm_raiseto (&sinproc->fsm, &sinproc->peer->fsm, &sinproc->event_sent,
        sinproc, NN_SINPROC_SENT);

    return 0;
}

static int nn_sinproc_recv (struct nn_pipebase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_sinproc *sinproc;

    sinproc = nn_cont (self, struct nn_sinproc, pipebase);

    /*  Sanity check. */
    nn_assert (sinproc->state == NN_SINPROC_STATE_ACTIVE);

    /*  Move the message to the caller. */
    rc = nn_msgqueue_recv (&sinproc->msgqueue, msg);
    errnum_assert (rc == 0, -rc);

    /*  If there was a message from peer lingering, try to push it to the
        queue once again. */
    if (nn_slow (sinproc->flags & NN_SINPROC_FLAG_RECEIVING)) {
        rc = nn_msgqueue_send (&sinproc->msgqueue, &sinproc->peer->msg);
        if (rc == -EAGAIN)
            return 0;
        errnum_assert (rc == 0, -rc);
        nn_msg_init (&sinproc->peer->msg, 0);
        nn_fsm_raiseto (&sinproc->fsm, &sinproc->peer->fsm,
            &sinproc->event_received, sinproc, NN_SINPROC_RECEIVED);
        sinproc->flags &= ~NN_SINPROC_FLAG_RECEIVING;
    }

    return 0;
}

void nn_sinproc_handler (struct nn_fsm *self, void *source, int type)
{
    int rc;
    struct nn_sinproc *sinproc;
    int empty;

    sinproc = nn_cont (self, struct nn_sinproc, fsm);

    switch (sinproc->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_SINPROC_STATE_IDLE:
        if (source == &sinproc->fsm) {
            switch (type) {
            case NN_FSM_START:
                sinproc->state = NN_SINPROC_STATE_CONNECTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTING state.                                                         */
/*  CONNECT request was sent to the peer. Now we are waiting for the          */
/*  acknowledgement.                                                          */
/******************************************************************************/
    case NN_SINPROC_STATE_CONNECTING:
        if (source == NULL) {
            switch (type) {
            case NN_SINPROC_ACTION_ACCEPTED:
                rc = nn_pipebase_start (&sinproc->pipebase);
                errnum_assert (rc == 0, -rc);
                sinproc->state = NN_SINPROC_STATE_ACTIVE;
                return;
            default:
                nn_assert (0);
            }
        }

        /*  The assumption here is that all the events are coming from the
            peer sinproc object. */
        switch (type) {
        case NN_SINPROC_ACCEPTED:
printf ("%p ACCEPTED received (by sinproc)\n", self);
            sinproc->peer = (struct nn_sinproc*) source;
            rc = nn_pipebase_start (&sinproc->pipebase);
            errnum_assert (rc == 0, -rc);
            sinproc->state = NN_SINPROC_STATE_ACTIVE;
            return;
        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/******************************************************************************/
        case NN_SINPROC_STATE_ACTIVE:
            if (source == sinproc->peer) {
                switch (type) {
                case NN_SINPROC_SENT:

                    empty = nn_msgqueue_empty (&sinproc->msgqueue);

                    /*  Push the message to the inbound message queue. */
                    rc = nn_msgqueue_send (&sinproc->msgqueue,
                        &sinproc->peer->msg);
                    if (rc == -EAGAIN) {
                        sinproc->flags |= NN_SINPROC_FLAG_RECEIVING;
                        return;
                    }
                    errnum_assert (rc == 0, -rc);
                    nn_msg_init (&sinproc->peer->msg, 0);

                    /*  Notify the user that there's a message to receive. */
                    if (empty)
                        nn_pipebase_received (&sinproc->pipebase);

                    /*  Notify the peer that the message was received. */
                    nn_fsm_raiseto (&sinproc->fsm, &sinproc->peer->fsm,
                        &sinproc->event_received, sinproc, NN_SINPROC_RECEIVED);

                    return;

                case NN_SINPROC_RECEIVED:
                    nn_assert (sinproc->flags & NN_SINPROC_FLAG_SENDING);
                    nn_pipebase_sent (&sinproc->pipebase);
                    sinproc->flags &= ~NN_SINPROC_FLAG_SENDING;
                    return;
                    
                default:
                    nn_assert (0);
                }
            }
            nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

