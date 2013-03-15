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

#include "astream.h"
#include "err.h"
#include "cont.h"
#include "alloc.h"

static const struct nn_cp_sink nn_astream_state_connected;
static const struct nn_cp_sink nn_astream_state_terminating;

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the 'stream' state machine. */

static void nn_astream_connected_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum);
static const struct nn_cp_sink nn_astream_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    nn_astream_connected_err,
    NULL,
    NULL,
    NULL
};

void nn_astream_init (struct nn_astream *self, struct nn_epbase *epbase,
    int s, struct nn_usock *usock, struct nn_bstream *bstream)
{
    int sndbuf;
    int rcvbuf;
    size_t sz;

    /*  Switch the state. */
    self->sink = &nn_astream_state_connected;
    self->bstream = bstream;

    /*  This stearm does not belong yet to the bstream. */
    nn_list_item_init (&self->item);

    /*  Get the current values of NN_SNDBUF and NN_RCVBUF options. */    
    sz = sizeof (sndbuf);
    nn_epbase_getopt (&self->bstream->epbase, NN_SOL_SOCKET, NN_SNDBUF,
        &sndbuf, &sz);
    nn_assert (sz == sizeof (sndbuf));
    sz = sizeof (rcvbuf);
    nn_epbase_getopt (&self->bstream->epbase, NN_SOL_SOCKET, NN_RCVBUF,
        &rcvbuf, &sz);
    nn_assert (sz == sizeof (rcvbuf));

    /*  Start the stream state machine. */
    nn_usock_init_child (&self->usock, usock, s, &self->sink, sndbuf, rcvbuf,
        usock->cp);
    
    /*  Note: must add myself to the astreams list *before* initializing my
        stream, which may fail and terminate me. */
    nn_list_insert (&bstream->astreams, &self->item,
        nn_list_end (&bstream->astreams));

    /*  Note: may fail and terminate me - do not reference self after
        this point! */
    nn_stream_init (&self->stream, epbase, &self->usock);
}

static void nn_astream_connected_err (const struct nn_cp_sink **self,
    struct nn_usock *usock, int errnum)
{
    struct nn_astream *astream;

    astream = nn_cont (self, struct nn_astream, sink);

    /*  Ask the underlying socket to terminate. */
    astream->sink = &nn_astream_state_terminating;
    nn_usock_close (&astream->usock);
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void nn_astream_terminating_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static const struct nn_cp_sink nn_astream_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_astream_terminating_closed,
    NULL,
    NULL
};

void nn_astream_close (struct nn_astream *self)
{
    /*  If termination is already underway, do nothing and let it continue. */
    if (self->sink == &nn_astream_state_terminating)
        return;

    /*  Terminate the associated session. */
    nn_stream_term (&self->stream);

    /*  Ask the underlying socket to terminate. */
    self->sink = &nn_astream_state_terminating;
    nn_usock_close (&self->usock);
}

static void nn_astream_terminating_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_astream *astream;

    astream = nn_cont (self, struct nn_astream, sink);

    /*  Ignore if I don't belong to the bstream. */
    if (nn_list_item_isinlist (&astream->item))
        nn_bstream_astream_closed (astream->bstream, astream);

    nn_list_item_term (&astream->item);
    nn_free (astream);
}

