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

static const struct sp_cp_sink sp_astream_state_connected;
static const struct sp_cp_sink sp_astream_state_terminating;

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the 'stream' state machine. */

static void sp_astream_connected_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum);
static const struct sp_cp_sink sp_astream_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    sp_astream_connected_err,
    NULL,
    NULL,
    NULL
};

void sp_astream_init (struct sp_astream *self, struct sp_epbase *epbase,
    int s, struct sp_usock *usock, struct sp_bstream *bstream)
{
    self->sink = &sp_astream_state_connected;
    self->bstream = bstream;
    sp_usock_init_child (&self->usock, usock, s, &self->sink, usock->cp);
    sp_stream_init (&self->stream, epbase, &self->usock);
}

static void sp_astream_connected_err (const struct sp_cp_sink **self,
    struct sp_usock *usock, int errnum)
{
    struct sp_astream *astream;

    astream = sp_cont (self, struct sp_astream, sink);

    /*  Ask the underlying socket to terminate. */
    astream->sink = &sp_astream_state_terminating;
    sp_usock_close (&astream->usock);
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_astream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_astream_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_astream_terminating_closed,
    NULL,
    NULL
};

int sp_astream_close (struct sp_astream *self)
{
    /*  Terminate the associated session. */
    sp_assert (self->sink == &sp_astream_state_connected);
    sp_stream_term (&self->stream);

    /*  Ask the underlying socket to terminate. */
    self->sink = &sp_astream_state_terminating;
    sp_usock_close (&self->usock);

    return 0;
}

static void sp_astream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_astream *astream;

    astream = sp_cont (self, struct sp_astream, sink);

    sp_list_erase (&astream->bstream->astreams, &astream->item);
    sp_free (astream);
}

