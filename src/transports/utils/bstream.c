/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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
#include "stream.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"

#include <string.h>

/*  This an internal structure used by bstream to represent individual
    accepted connections. Note that it is a POD, not a full-blown class. */
struct nn_astream {
    struct nn_usock usock;
    struct nn_stream stream;
    struct nn_list_item item;
};

#define NN_BSTREAM_STATE_INIT 1
#define NN_BSTREAM_STATE_ACTIVE 2
#define NN_BSTREAM_STATE_CLOSING 3

#define NN_BSTREAM_EVENT_START 1

/*  Private functions. */
static void nn_bstream_callback (struct nn_fsm *fsm, void *source, int type);

/*  Implementation of nn_epbase interface. */
static int nn_bstream_close (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_bstream_epbase_vfptr =
    {nn_bstream_close};

int nn_bstream_init (struct nn_bstream *self, const char *addr, void *hint,
    const struct nn_bstream_vfptr *vfptr)
{
    int rc;

    self->vfptr = vfptr;

    nn_epbase_init (&self->epbase, &nn_bstream_epbase_vfptr, addr, hint);

    nn_fsm_init_root (&self->fsm, nn_bstream_callback,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_BSTREAM_STATE_INIT;

    /*  Open the listening socket. */
    rc = self->vfptr->open (addr, &self->usock, &self->fsm);
    if (nn_slow (rc < 0)) {
        nn_fsm_term (&self->fsm);
        nn_epbase_term (&self->epbase);
        return rc;
    }

    self->astream = NULL;
    nn_list_init (&self->astreams);

    /*  Notify the state machine. */
    nn_bstream_callback (&self->fsm, NULL, NN_BSTREAM_EVENT_START);

    return 0;
}

static int nn_bstream_close (struct nn_epbase *self)
{
    nn_assert (0);
}

static void nn_bstream_callback (struct nn_fsm *fsm, void *source, int type)
{
    struct nn_bstream *bstream;

    bstream = nn_cont (fsm, struct nn_bstream, fsm);

    switch (bstream->state) {

/******************************************************************************/
/*  INIT state                                                                */
/******************************************************************************/
    case NN_BSTREAM_STATE_INIT:
        if (source == NULL) {
            switch (type) {
            case NN_BSTREAM_EVENT_START:

                /*  Start waiting for incoming connection. */
                bstream->astream = nn_alloc (sizeof (struct nn_astream),
                    "astream");
                alloc_assert (bstream->astream);
                nn_list_item_init (&bstream->astream->item);
                nn_usock_accept (&bstream->usock, &bstream->astream->usock,
                    &bstream->fsm);
                bstream->state = NN_BSTREAM_STATE_ACTIVE;
                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACTIVE state                                                              */
/******************************************************************************/
    case NN_BSTREAM_STATE_ACTIVE:
        if (source == &bstream->usock) {
            switch (type) {
            case NN_USOCK_ACCEPTED:

                /*  New connecting arrived. Start its state machine. */
                nn_stream_init (&bstream->astream->stream, &bstream->epbase,
                    &bstream->astream->usock, &bstream->fsm);
                nn_list_insert (&bstream->astreams, &bstream->astream->item,
                    nn_list_end (&bstream->astreams));
                bstream->astream = NULL;
                bstream->state = NN_BSTREAM_STATE_INIT;
                nn_bstream_callback (&bstream->fsm, NULL,
                    NN_BSTREAM_EVENT_START);
                return; 
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING state                                                             */
/******************************************************************************/
    case NN_BSTREAM_STATE_CLOSING:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state                                                             */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

#if 0

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
    struct nn_aio_usock *usock, int s);
static const struct nn_cp_sink nn_bstream_state_listening = {
    NULL,
    NULL,
    NULL,
    nn_bstream_listening_accepted,
    NULL,
    NULL
};

int nn_bstream_init (struct nn_bstream *self, const char *addr, void *hint,
    int (*initfn) (const char *addr, struct nn_aio_usock *usock,
    struct nn_cp *cp, int backlog), int backlog)
{
    int rc;

    /*  Start in LISTENING state. */
    self->sink = &nn_bstream_state_listening;
    nn_list_init (&self->astreams);
    nn_epbase_init (&self->epbase, &nn_bstream_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = initfn (addr, &self->usock, nn_epbase_getcp (&self->epbase), backlog);
    nn_aio_usock_setsink (&self->usock, &self->sink);
    if (nn_slow (rc < 0)) {
        nn_epbase_term (&self->epbase);
        nn_list_term (&self->astreams);
        return rc;
    }

    /*  Start waiting for incoming connection. */
    nn_aio_usock_accept (&self->usock);

    return 0;
}

static void nn_bstream_listening_accepted (const struct nn_cp_sink **self,
    struct nn_aio_usock *usock, int s)
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
    struct nn_aio_usock *usock);
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
    nn_aio_usock_close (&bstream->usock);

    return -EINPROGRESS;
}

static void nn_bstream_terminating1_closed (const struct nn_cp_sink **self,
    struct nn_aio_usock *usock)
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

#endif
