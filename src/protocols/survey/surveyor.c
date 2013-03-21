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

#include "surveyor.h"
#include "xsurveyor.h"

#include "../../nn.h"
#include "../../survey.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/wire.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"
#include "../../utils/list.h"

#include <stdint.h>
#include <string.h>

#define NN_SURVEYOR_DEFAULT_DEADLINE 1000

#define NN_SURVEYOR_INPROGRESS 1

struct nn_surveyor {
    struct nn_xsurveyor xsurveyor;
    const struct nn_cp_sink *sink;
    uint32_t flags;
    uint32_t surveyid;
    int deadline;
    struct nn_timer deadline_timer;
};

/*  Private functions. */
static int nn_surveyor_init (struct nn_surveyor *self,
    const struct nn_sockbase_vfptr *vfptr);
static void nn_surveyor_term (struct nn_surveyor *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_surveyor_destroy (struct nn_sockbase *self);
static int nn_surveyor_events (struct nn_sockbase *self);
static int nn_surveyor_send (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_surveyor_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_surveyor_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_surveyor_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_surveyor_sockbase_vfptr = {
    0,
    nn_xsurveyor_ispeer,
    nn_surveyor_destroy,
    nn_xsurveyor_add,
    nn_xsurveyor_rm,
    nn_xsurveyor_in,
    nn_xsurveyor_out,
    nn_surveyor_events,
    nn_surveyor_send,
    nn_surveyor_recv,
    nn_surveyor_setopt,
    nn_surveyor_getopt
};

/*  Event sink. */
static void nn_surveyor_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer);
static const struct nn_cp_sink nn_surveyor_sink = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    nn_surveyor_timeout
};

static int nn_surveyor_init (struct nn_surveyor *self,
    const struct nn_sockbase_vfptr *vfptr)
{
    int rc;

    rc = nn_xsurveyor_init (&self->xsurveyor, vfptr);
    if (rc < 0)
        return rc;

    self->sink = &nn_surveyor_sink;
    self->flags = 0;

    /*  Start assigning survey IDs beginning with a random number. This way
        there should be no key clashes even if the executable is re-started. */
    nn_random_generate (&self->surveyid, sizeof (self->surveyid));

    self->deadline = NN_SURVEYOR_DEFAULT_DEADLINE;
    nn_timer_init (&self->deadline_timer, &self->sink,
        nn_sockbase_getcp (&self->xsurveyor.sockbase));

    return 0;
}

static void nn_surveyor_term (struct nn_surveyor *self)
{
    nn_timer_term (&self->deadline_timer);
    nn_xsurveyor_term (&self->xsurveyor);
}

void nn_surveyor_destroy (struct nn_sockbase *self)
{
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    nn_surveyor_term (surveyor);
    nn_free (surveyor);
}

static int nn_surveyor_events (struct nn_sockbase *self)
{
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    if (!(surveyor->flags & NN_SURVEYOR_INPROGRESS))
        return NN_SOCKBASE_EVENT_IN | NN_SOCKBASE_EVENT_OUT;
    return nn_xsurveyor_events (&surveyor->xsurveyor.sockbase);
}

static int nn_surveyor_send (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    /*  Cancel any ongoing survey. */
    if (nn_slow (surveyor->flags & NN_SURVEYOR_INPROGRESS)) {
        surveyor->flags &= ~NN_SURVEYOR_INPROGRESS;
        nn_timer_stop (&surveyor->deadline_timer);
    }

    /*  Generate new survey ID. */
    ++surveyor->surveyid;

    /*  Tag the survey body with survey ID. */
    nn_assert (nn_chunkref_size (&msg->hdr) == 0);
    nn_chunkref_term (&msg->hdr);
    nn_chunkref_init (&msg->hdr, 4);
    nn_putl (nn_chunkref_data (&msg->hdr), surveyor->surveyid);

    /*  Send the survey. */
    rc = nn_xsurveyor_send (&surveyor->xsurveyor.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    surveyor->flags |= NN_SURVEYOR_INPROGRESS;

    /*  Set up the re-send timer. */
    nn_timer_start (&surveyor->deadline_timer, surveyor->deadline);

    return 0;
}

static int nn_surveyor_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_surveyor *surveyor;
    uint32_t surveyid;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    /*  If no survey is going on return EFSM error. */
    if (nn_slow (!(surveyor->flags & NN_SURVEYOR_INPROGRESS)))
       return -EFSM;

    while (1) {

        /*  Get next response. */
        rc = nn_xsurveyor_recv (&surveyor->xsurveyor.sockbase, msg);
        if (nn_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc == 0, -rc);

        /*  Get the survey ID. Ignore any stale responses. */
        if (nn_slow (nn_chunkref_size (&msg->hdr) != sizeof (uint32_t))) {
            nn_msg_term (msg);
            continue;
        }
        surveyid = nn_getl (nn_chunkref_data (&msg->hdr));
        if (nn_slow (surveyid != surveyor->surveyid)) {
            nn_msg_term (msg);
            continue;
        }

        /*  Discard the header and return the message to the user. */
        nn_chunkref_term (&msg->hdr);
        nn_chunkref_init (&msg->hdr, 0);
        break;
    }

    return 0;
}

static void nn_surveyor_timeout (const struct nn_cp_sink **self,
    struct nn_timer *timer)
{
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, sink);

    /*  Cancel the survey. */
    surveyor->flags &= ~NN_SURVEYOR_INPROGRESS;

    /*  If there's a blocked recv/poll operation, unblock it. */
    nn_sockbase_changed (&surveyor->xsurveyor.sockbase);
}

static int nn_surveyor_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen)
{
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    if (level != NN_SURVEYOR)
        return -ENOPROTOOPT;

    if (option == NN_SURVEYOR_DEADLINE) {
        if (nn_slow (optvallen != sizeof (int)))
            return -EINVAL;
        surveyor->deadline = *(int*) optval;
        return 0;
    }

    return -ENOPROTOOPT;
}

static int nn_surveyor_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    struct nn_surveyor *surveyor;

    surveyor = nn_cont (self, struct nn_surveyor, xsurveyor.sockbase);

    if (level != NN_SURVEYOR)
        return -ENOPROTOOPT;

    if (option == NN_SURVEYOR_DEADLINE) {
        if (nn_slow (*optvallen < sizeof (int)))
            return -EINVAL;
        *(int*) optval = surveyor->deadline;
        *optvallen = sizeof (int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static int nn_surveyor_create (struct nn_sockbase **sockbase)
{
    int rc;
    struct nn_surveyor *self;

    self = nn_alloc (sizeof (struct nn_surveyor), "socket (surveyor)");
    alloc_assert (self);
    rc = nn_surveyor_init (self, &nn_surveyor_sockbase_vfptr);
    if (rc < 0) {
        nn_free (self);
        return rc;
    }
    *sockbase = &self->xsurveyor.sockbase;

    return 0;
}

static struct nn_socktype nn_surveyor_socktype_struct = {
    AF_SP,
    NN_SURVEYOR,
    nn_surveyor_create,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_surveyor_socktype = &nn_surveyor_socktype_struct;

