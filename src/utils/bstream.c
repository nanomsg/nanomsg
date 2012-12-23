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

/*  Implementation of sp_epbase interface. */
static int sp_bstream_close (struct sp_epbase *self, int linger);
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
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_bstream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_bstream_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_bstream_terminating_closed,
    NULL,
    NULL
};

static int sp_bstream_close (struct sp_epbase *self, int linger)
{
    struct sp_bstream *bstream;
    struct sp_list_item *it;

    bstream = sp_cont (self, struct sp_bstream, epbase);

    /*  First, ask all the associated sessions to close. */
    for (it = sp_list_begin (&bstream->astreams);
          it != sp_list_end (&bstream->astreams);
          it = sp_list_next (&bstream->astreams, it))
        sp_astream_close (sp_cont (it, struct sp_astream, item));

    /*  Close the listening socket itself. */
    bstream->sink = &sp_bstream_state_terminating;
    sp_usock_close (&bstream->usock);

    return 0;
}

static void sp_bstream_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_bstream *bstream;

    bstream = sp_cont (self, struct sp_bstream, sink);

    sp_epbase_term (&bstream->epbase);
    sp_free (bstream);
}

