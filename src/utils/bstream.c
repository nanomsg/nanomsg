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
static void sp_bstream_term (struct sp_bstream *self);

/*  States. */
static const struct sp_cp_sink sp_bstream_state_listening;
static const struct sp_cp_sink sp_bstream_state_terminating1;
static const struct sp_cp_sink sp_bstream_state_terminating2;

/*  Implementation of sp_epbase interface. */
static void sp_bstream_close (struct sp_epbase *self);
static const struct sp_epbase_vfptr sp_bstream_epbase_vfptr =
    {sp_bstream_close};

/******************************************************************************/
/*  State: LISTENING                                                          */
/******************************************************************************/

static void sp_bstream_listening_accepted (const struct sp_cp_sink **self,
    struct sp_usock *usock, int s);
static const struct sp_cp_sink sp_bstream_state_listening = {
    NULL,
    NULL,
    NULL,
    sp_bstream_listening_accepted,
    NULL,
    NULL
};

int sp_bstream_init (struct sp_bstream *self, const char *addr, void *hint,
    int (*initfn) (const char *addr, struct sp_usock *usock, struct sp_cp *cp,
    int backlog), int backlog)
{
    int rc;

    /*  Start in LISTENING state. */
    self->sink = &sp_bstream_state_listening;
    sp_list_init (&self->astreams);
    sp_epbase_init (&self->epbase, &sp_bstream_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = initfn (addr, &self->usock, sp_epbase_getcp (&self->epbase), backlog);
    sp_usock_setsink (&self->usock, &self->sink);
    if (sp_slow (rc < 0)) {
        sp_epbase_term (&self->epbase);
        sp_list_term (&self->astreams);
        return rc;
    }

    /*  Start waiting for incoming connection. */
    sp_usock_accept (&self->usock);

    return 0;
}

static void sp_bstream_listening_accepted (const struct sp_cp_sink **self,
    struct sp_usock *usock, int s)
{
    struct sp_bstream *bstream;
    struct sp_astream *astream;

    bstream = sp_cont (self, struct sp_bstream, sink);

    astream = sp_alloc (sizeof (struct sp_astream), "astream");
    alloc_assert (astream);
    sp_astream_init (astream, &bstream->epbase, s, usock, bstream);
    sp_list_insert (&bstream->astreams, &astream->item,
        sp_list_end (&bstream->astreams));
}

/******************************************************************************/
/*  State: TERMINATING1                                                       */
/******************************************************************************/

static void sp_bstream_terminating1_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_bstream_state_terminating1 = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_bstream_terminating1_closed,
    NULL,
    NULL
};

static void sp_bstream_close (struct sp_epbase *self)
{
    struct sp_bstream *bstream;

    bstream = sp_cont (self, struct sp_bstream, epbase);

    /*  Close the listening socket itself. */
    bstream->sink = &sp_bstream_state_terminating1;
    sp_usock_close (&bstream->usock);
}

static void sp_bstream_terminating1_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_bstream *bstream;
    struct sp_list_item *it;
    struct sp_astream *astream;

    bstream = sp_cont (self, struct sp_bstream, sink);

    /*  Listening socket is closed. Switch from TERMINATING1 to TERMINATING2
        state and start closing individual sessions. */
    sp_assert (bstream->sink == &sp_bstream_state_terminating1);
    bstream->sink = &sp_bstream_state_terminating2;

    /*  Ask all the associated sessions to close. */
    it = sp_list_begin (&bstream->astreams);
    while (it != sp_list_end (&bstream->astreams)) {
        astream = sp_cont (it, struct sp_astream, item);
        it = sp_list_next (&bstream->astreams, it);
        sp_astream_close (astream);
    }

    /*  If there are no sessions left, we can terminate straight away. */
    if (sp_list_empty (&bstream->astreams)) {
        sp_bstream_term (bstream);
        sp_free (bstream);
        return;
    }
}

/******************************************************************************/
/*  State: TERMINATING2                                                       */
/******************************************************************************/

static const struct sp_cp_sink sp_bstream_state_terminating2 = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

void sp_bstream_astream_closed (struct sp_bstream *self,
    struct sp_astream *astream)
{
    /*  One of the associated astreams was closed. */
    sp_list_erase (&self->astreams, &astream->item);

    /*  In TERMINATING state this may be the last astream left.
        If so, we can move on with the deallocation. */
    if (self->sink == &sp_bstream_state_terminating2 &&
          sp_list_empty (&self->astreams)) {
        sp_bstream_term (self);
        sp_free (self);
        return;
    }
}

static void sp_bstream_term (struct sp_bstream *self)
{
    sp_epbase_term (&self->epbase);
}

