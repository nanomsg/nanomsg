/*
    Copyright (c) 2012 250bpm s.r.o.

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

#include "tcps.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/wire.h"
#include "../../utils/fast.h"

#include <string.h>
#include <stdint.h>

/*   Private functions. */
static void sp_tcps_hdr_received (const struct sp_sink **self,
    struct sp_usock *usock);
static void sp_tcps_hdr_sent (const struct sp_sink **self,
    struct sp_usock *usock);
static void sp_tcps_hdr_timeout (const struct sp_sink **self,
    struct sp_timer *timer);
static void sp_tcps_activate (struct sp_tcps *self);
static void sp_tcps_received (const struct sp_sink **self,
    struct sp_usock *usock);
static void sp_tcps_sent (const struct sp_sink **self,
    struct sp_usock *usock);
static void sp_tcps_err (const struct sp_sink **self,
    struct sp_usock *usock, int errnum);

/*  START state. */
static const struct sp_sink sp_tcps_state_start = {
    sp_tcps_hdr_received,
    sp_tcps_hdr_sent,
    NULL,
    NULL,
    sp_tcps_err,
    sp_tcps_hdr_timeout
};

/*  SENT state. */
static const struct sp_sink sp_tcps_state_sent = {
    sp_tcps_hdr_received,
    NULL,
    NULL,
    NULL,
    sp_tcps_err,
    sp_tcps_hdr_timeout
};

/*  RECEIVED state. */
static const struct sp_sink sp_tcps_state_received = {
    NULL,
    sp_tcps_hdr_sent,
    NULL,
    NULL,
    sp_tcps_err,
    sp_tcps_hdr_timeout
};

/*  ACTIVE state. */
static const struct sp_sink sp_tcps_state_active = {
    sp_tcps_received,
    sp_tcps_sent,
    NULL,
    NULL,
    sp_tcps_err,
    NULL
};

/*  Pipe interface. */
int sp_tcps_send (struct sp_pipebase *self, const void *buf, size_t len);
int sp_tcps_recv (struct sp_pipebase *self, void *buf, size_t *len);

const struct sp_pipebase_vfptr sp_tcps_pipebase_vfptr = {
    sp_tcps_send,
    sp_tcps_recv
};

void sp_tcps_init (struct sp_tcps *self, struct sp_epbase *epbase,
    struct sp_usock *usock)
{
    int rc;

    /*  Redirect the underlying socket's events to this state machine. */
    self->usock = usock;
    self->sink = &sp_tcps_state_start;
    self->original_sink = sp_usock_setsink (usock, &self->sink);

    /*  Initialise the pipe to communicate with the user. */
    /*  TODO: Socket type may reject the pipe. What then? */
    rc = sp_pipebase_init (&self->pipebase, &sp_tcps_pipebase_vfptr, epbase);
    sp_assert (rc == 0);

    /*  Start the header timeout timer. */
    sp_timer_init (&self->hdr_timeout, &self->sink, usock->cp);
    sp_timer_start (&self->hdr_timeout, 1000);

    /*  Send the protocol header. */
    sp_usock_send (usock, "\0\0SP\0\0\0\0", 8);

    /*  Receive the protocol header from the peer. */
    sp_usock_recv (usock, self->hdr, 8);
}

void sp_tcps_term ()
{
    sp_assert (0);
}

static void sp_tcps_hdr_received (const struct sp_sink **self,
    struct sp_usock *usock)
{
    struct sp_tcps *tcps;

    tcps = sp_cont (self, struct sp_tcps, sink);

    if (tcps->sink == &sp_tcps_state_sent) {
        sp_tcps_activate (tcps);
        return;
    }

    if (tcps->sink == &sp_tcps_state_start) {
        tcps->sink = &sp_tcps_state_received;
        return;
    }

    /*  This event is not defined in other states. */
    sp_assert (0);
}

static void sp_tcps_hdr_sent (const struct sp_sink **self,
    struct sp_usock *usock)
{
    struct sp_tcps *tcps;

    tcps = sp_cont (self, struct sp_tcps, sink);

    if (tcps->sink == &sp_tcps_state_received) {
        sp_tcps_activate (tcps);
        return;
    }

    if (tcps->sink == &sp_tcps_state_start) {
        tcps->sink = &sp_tcps_state_sent;
        return;
    }

    /*  This event is not defined in other states. */
    sp_assert (0);
}

static void sp_tcps_hdr_timeout (const struct sp_sink **self,
    struct sp_timer *timer)
{
    /*  TODO: Header timeout. Drop the connection here. */
    sp_assert (0);
}

static void sp_tcps_activate (struct sp_tcps *self)
{
    self->sink = &sp_tcps_state_active;
    sp_timer_stop (&self->hdr_timeout);

    /*  Check the header. */
    /*  TODO: If it does not conform, drop the connection. */
    if (memcmp (self->hdr, "\0\0SP\0\0\0\0", 8) != 0)
        sp_assert (0);

    /*  Connection is ready for sending. Make outpipe available
        to the SP socket. */
    sp_pipebase_out (&self->pipebase);

    /*  Start waiting for incoming messages. First, read the 8-byte size. */
    self->instate = SP_TCPS_INSTATE_HDR;
    sp_usock_recv (self->usock, self->inhdr, 8);
}

static void sp_tcps_received (const struct sp_sink **self,
    struct sp_usock *usock)
{
    int rc;
    struct sp_tcps *tcps;
    uint64_t size;

    tcps = sp_cont (self, struct sp_tcps, sink);
    switch (tcps->instate) {
    case SP_TCPS_INSTATE_HDR:
        size = sp_getll (tcps->inhdr);
        rc = sp_msg_init (&tcps->inmsg, size);
        errnum_assert (rc == 0, -rc);
        tcps->instate = SP_TCPS_INSTATE_BODY;
        sp_usock_recv (tcps->usock, sp_msg_data (&tcps->inmsg), size);
        break;
    case SP_TCPS_INSTATE_BODY:
        sp_assert (0);
        break;
    default:
        sp_assert (0);
    }
}

static void sp_tcps_sent (const struct sp_sink **self,
    struct sp_usock *usock)
{
    struct sp_tcps *tcps;

    tcps = sp_cont (self, struct sp_tcps, sink);
    switch (tcps->outstate) {
    case SP_TCPS_OUTSTATE_HDR:
        tcps->outstate = SP_TCPS_OUTSTATE_BODY;
        sp_usock_send (tcps->usock, sp_msg_data (&tcps->outmsg),
            sp_msg_size (&tcps->outmsg));
        break;
    case SP_TCPS_OUTSTATE_BODY:
        sp_assert (0);
        break;
    default:
        sp_assert (0);
    }
}

static void sp_tcps_err (const struct sp_sink **self,
    struct sp_usock *usock, int errnum)
{
    sp_assert (0);
}

int sp_tcps_send (struct sp_pipebase *self, const void *buf, size_t len)
{
    int rc;
    struct sp_tcps *tcps;

    tcps = sp_cont (self, struct sp_tcps, pipebase);

    /*  Make a local copy of the message. */
    rc = sp_msg_init (&tcps->outmsg, len);
    if (sp_slow (rc < 0))
        return rc;
    memcpy (sp_msg_data (&tcps->outmsg), buf, len);

    /*  Serialise the message header. */
    sp_putll (tcps->outhdr, len);

    /*  Start the outbound state machine. */
    tcps->outstate = SP_TCPS_OUTSTATE_HDR;
    sp_usock_send (tcps->usock, tcps->outhdr, 8);

    return 0;
}

int sp_tcps_recv (struct sp_pipebase *self, void *buf, size_t *len)
{
    sp_assert (0);
}

