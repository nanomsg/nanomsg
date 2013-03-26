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

#include "bstream.h"
#include "astream.h"
#include "err.h"
#include "cont.h"
#include "addr.h"
#include "alloc.h"
#include "fast.h"

#include <string.h>

/*  Private functions. */
static void nn_bstream_term (struct nn_bstream *self);

/*  States. */
static const struct nn_cp_sink nn_bstream_state_listening;
static const struct nn_cp_sink nn_bstream_state_terminating1;
static const struct nn_cp_sink nn_bstream_state_terminating2;

/*  Implementation of nn_epbase interface. */
static int nn_bstream_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_bstream_epbase_vfptr =
    {nn_bstream_close};

/******************************************************************************/
/*  State: LISTENING                                                          */
/******************************************************************************/

static void nn_bstream_listening_accepted (const struct nn_cp_sink **self,
    struct nn_usock *usock, int s);
static const struct nn_cp_sink nn_bstream_state_listening = {
    NULL,
    NULL,
    NULL,
    nn_bstream_listening_accepted,
    NULL,
    NULL
};

int nn_bstream_init (struct nn_bstream *self, const char *addr, void *hint,
    int (*initfn) (const char *addr, struct nn_usock *usock, struct nn_cp *cp,
    int backlog), int backlog)
{
    int rc;

    /*  Start in LISTENING state. */
    self->sink = &nn_bstream_state_listening;
    nn_list_init (&self->astreams);
    nn_epbase_init (&self->epbase, &nn_bstream_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = initfn (addr, &self->usock, nn_epbase_getcp (&self->epbase), backlog);
    nn_usock_setsink (&self->usock, &self->sink);
    if (nn_slow (rc < 0)) {
        nn_epbase_term (&self->epbase);
        nn_list_term (&self->astreams);
        return rc;
    }

    /*  Start waiting for incoming connection. */
    nn_usock_accept (&self->usock);

    return 0;
}

static void nn_bstream_listening_accepted (const struct nn_cp_sink **self,
    struct nn_usock *usock, int s)
{
    struct nn_bstream *bstream;
    struct nn_astream *astream;

    bstream = nn_cont (self, struct nn_bstream, sink);

    astream = nn_alloc (sizeof (struct nn_astream), "astream");
    alloc_assert (astream);
    
    /*  Note: astream may be terminated after this call -
        do not reference it. */
    nn_astream_init (astream, &bstream->epbase, s, usock, bstream);
}

/******************************************************************************/
/*  State: TERMINATING1                                                       */
/******************************************************************************/

static void nn_bstream_terminating1_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock);
static const struct nn_cp_sink nn_bstream_state_terminating1 = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_bstream_terminating1_closed,
    NULL,
    NULL
};

static int nn_bstream_close (struct nn_epbase *self)
{
    struct nn_bstream *bstream;

    bstream = nn_cont (self, struct nn_bstream, epbase);

    /*  Close the listening socket itself. */
    bstream->sink = &nn_bstream_state_terminating1;
    nn_usock_close (&bstream->usock);

    return -EINPROGRESS;
}

static void nn_bstream_terminating1_closed (const struct nn_cp_sink **self,
    struct nn_usock *usock)
{
    struct nn_bstream *bstream;
    struct nn_list_item *it;
    struct nn_astream *astream;

    bstream = nn_cont (self, struct nn_bstream, sink);

    /*  Listening socket is closed. Switch from TERMINATING1 to TERMINATING2
        state and start closing individual sessions. */
    nn_assert (bstream->sink == &nn_bstream_state_terminating1);
    bstream->sink = &nn_bstream_state_terminating2;

    /*  Ask all the associated sessions to close. */
    it = nn_list_begin (&bstream->astreams);
    while (it != nn_list_end (&bstream->astreams)) {
        astream = nn_cont (it, struct nn_astream, item);
        it = nn_list_next (&bstream->astreams, it);
        nn_astream_close (astream);
    }

    /*  If there are no sessions left, we can terminate straight away. */
    if (nn_list_empty (&bstream->astreams)) {
        nn_bstream_term (bstream);
        nn_free (bstream);
        return;
    }
}

/******************************************************************************/
/*  State: TERMINATING2                                                       */
/******************************************************************************/

static const struct nn_cp_sink nn_bstream_state_terminating2 = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

void nn_bstream_astream_closed (struct nn_bstream *self,
    struct nn_astream *astream)
{
    /*  One of the associated astreams was closed. */
    nn_list_erase (&self->astreams, &astream->item);

    /*  In TERMINATING state this may be the last astream left.
        If so, we can move on with the deallocation. */
    if (self->sink == &nn_bstream_state_terminating2 &&
          nn_list_empty (&self->astreams)) {
        nn_bstream_term (self);
        nn_free (self);
        return;
    }
}

static void nn_bstream_term (struct nn_bstream *self)
{
    nn_epbase_term (&self->epbase);
}

