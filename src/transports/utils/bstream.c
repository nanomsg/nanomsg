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
#define NN_BSTREAM_STATE_CLOSING_USOCK 3
#define NN_BSTREAM_STATE_CLOSING_STREAMS 4
#define NN_BSTREAM_STATE_CLOSED 5

#define NN_BSTREAM_EVENT_START 1
#define NN_BSTREAM_EVENT_CLOSE 2

/*  Private functions. */
static void nn_bstream_term (struct nn_bstream *self);
static void nn_bstream_callback (struct nn_fsm *fsm, void *source, int type);

/*  Implementation of nn_epbase virtual interface. */
static void nn_bstream_close (struct nn_epbase *self);
static void nn_bstream_destroy (struct nn_epbase *self);
static const struct nn_epbase_vfptr nn_bstream_epbase_vfptr = {
     nn_bstream_close,
     nn_bstream_destroy
};

int nn_bstream_create (const struct nn_bstream_vfptr *vfptr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_bstream *self;

    self = nn_alloc (sizeof (struct nn_bstream), "bstream (ipc)");
    alloc_assert (self);

    self->vfptr = vfptr;

    nn_epbase_init (&self->epbase, &nn_bstream_epbase_vfptr, hint);

    nn_fsm_init_root (&self->fsm, nn_bstream_callback,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_BSTREAM_STATE_INIT;

    /*  Open the listening socket. */
    rc = self->vfptr->open (nn_epbase_getaddr (&self->epbase), &self->usock,
        &self->fsm);
    if (nn_slow (rc < 0)) {
        nn_fsm_term (&self->fsm);
        nn_epbase_term (&self->epbase);
        return rc;
    }

    self->astream = NULL;
    nn_list_init (&self->astreams);

    /*  Notify the state machine. */
    nn_bstream_callback (&self->fsm, NULL, NN_BSTREAM_EVENT_START);

    *epbase = &self->epbase;
    return 0;
}

static void nn_bstream_term (struct nn_bstream *self)
{
    nn_assert (self->state == NN_BSTREAM_STATE_CLOSED);

    nn_list_term (&self->astreams);
    if (self->astream)
       nn_free (self->astream);
    nn_epbase_term (&self->epbase);
    nn_fsm_term (&self->fsm);
}

static void nn_bstream_close (struct nn_epbase *self)
{
    struct nn_bstream *bstream;

    bstream = nn_cont (self, struct nn_bstream, epbase);

    /*  Pass the event to the state machine. */
    nn_bstream_callback (&bstream->fsm, NULL, NN_BSTREAM_EVENT_CLOSE);
}

static void nn_bstream_destroy (struct nn_epbase *self)
{
    struct nn_bstream *bstream;

    bstream = nn_cont (self, struct nn_bstream, epbase);

    nn_bstream_term (bstream);
    nn_free (bstream);
}

static void nn_bstream_callback (struct nn_fsm *fsm, void *source, int type)
{
    struct nn_bstream *bstream;
    struct nn_list_item *it;
    struct nn_astream *astream;

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
        if (source == NULL) {
            switch (type) {
            case NN_BSTREAM_EVENT_CLOSE:

                /*  User asked the object to be closed. First we'll close
                    the listening socket so that new connections cannot be
                    accepted. */
                nn_usock_close (&bstream->usock);
                bstream->state = NN_BSTREAM_STATE_CLOSING_USOCK;

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_USOCK state                                                       */
/******************************************************************************/
    case NN_BSTREAM_STATE_CLOSING_USOCK:
        if (source == &bstream->usock) {
            switch (type) {
            case NN_USOCK_CLOSED:

                /*  Deallocate the listening socket. */
                nn_usock_term (&bstream->usock);

                /*  Start shutting down individual accepted connections. */
                for (it = nn_list_begin (&bstream->astreams);
                      it != nn_list_end (&bstream->astreams);
                      it = nn_list_next (&bstream->astreams, it)) {
                    astream = nn_cont (it, struct nn_astream, item);
                    nn_stream_close (&astream->stream);
                }
                bstream->state = NN_BSTREAM_STATE_CLOSING_STREAMS;

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CLOSING_STREAMS state                                                     */
/******************************************************************************/
    case NN_BSTREAM_STATE_CLOSING_STREAMS:

        /*  At this point we assume that the event came either from one of the
            streams or one of the sockets being shut down. We could actually
            check whether that is the case, but the check would have O(n)
            complexity. */
        switch (type) {
        case NN_STREAM_CLOSED:
             astream = nn_cont (source, struct nn_astream, stream);
             nn_stream_term (&astream->stream);
             nn_usock_close (&astream->usock);
             return;
        case NN_USOCK_CLOSED:
             astream = nn_cont (source, struct nn_astream, usock);
             nn_usock_term (&astream->usock);
             nn_list_erase (&bstream->astreams, &astream->item);
             nn_free (astream);
             if (!nn_list_empty (&bstream->astreams))
                 return;

             bstream->state = NN_BSTREAM_STATE_CLOSED;
             nn_epbase_closed (&bstream->epbase);
             
             return;

        default:
            nn_assert (0);
        }

/******************************************************************************/
/*  CLOSED state                                                              */
/******************************************************************************/
    case NN_BSTREAM_STATE_CLOSED:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state                                                             */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

