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

#include "../../sp.h"
#include "../../survey.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/wire.h"
#include "../../utils/alloc.h"
#include "../../utils/random.h"

#include <stdint.h>
#include <string.h>

#define SP_SURVEYOR_DEFAULT_DEADLINE 1000

#define SP_SURVEYOR_INPROGRESS 1

struct sp_surveyor {
    struct sp_xsurveyor xsurveyor;
    const struct sp_cp_sink *sink;
    uint32_t flags;
    uint32_t surveyid;
    int deadline;
    struct sp_timer deadline_timer;
};

/*  Private functions. */
static void sp_surveyor_init (struct sp_surveyor *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);
static void sp_surveyor_term (struct sp_surveyor *self);

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_surveyor_destroy (struct sp_sockbase *self);
static int sp_surveyor_send (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_surveyor_recv (struct sp_sockbase *self, struct sp_msg *msg);
static int sp_surveyor_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_surveyor_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static int sp_surveyor_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen);
static int sp_surveyor_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen);
static const struct sp_sockbase_vfptr sp_surveyor_sockbase_vfptr = {
    sp_surveyor_destroy,
    sp_xsurveyor_add,
    sp_xsurveyor_rm,
    sp_xsurveyor_in,
    sp_xsurveyor_out,
    sp_surveyor_send,
    sp_surveyor_recv,
    sp_surveyor_setopt,
    sp_surveyor_getopt,
    sp_surveyor_sethdr,
    sp_surveyor_gethdr
};

/*  Event sink. */
static void sp_surveyor_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer);
static const struct sp_cp_sink sp_surveyor_sink = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_surveyor_timeout
};

static void sp_surveyor_init (struct sp_surveyor *self,
    const struct sp_sockbase_vfptr *vfptr, int fd)
{
    sp_xsurveyor_init (&self->xsurveyor, vfptr, fd);
    self->sink = &sp_surveyor_sink;
    self->flags = 0;

    /*  Start assigning survey IDs beginning with a random number. This way
        there should be no key clashes even if the executable is re-started. */
    sp_random_generate (&self->surveyid, sizeof (self->surveyid));

    self->deadline = SP_SURVEYOR_DEFAULT_DEADLINE;
    sp_timer_init (&self->deadline_timer, &self->sink,
        sp_sockbase_getcp (&self->xsurveyor.sockbase));
}

static void sp_surveyor_term (struct sp_surveyor *self)
{
    sp_timer_term (&self->deadline_timer);
    sp_xsurveyor_term (&self->xsurveyor);
}

void sp_surveyor_destroy (struct sp_sockbase *self)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    sp_surveyor_term (surveyor);
    sp_free (surveyor);
}

static int sp_surveyor_send (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    /*  Cancel any ongoing survey. */
    if (sp_slow (surveyor->flags & SP_SURVEYOR_INPROGRESS)) {
        surveyor->flags &= ~SP_SURVEYOR_INPROGRESS;
        sp_timer_stop (&surveyor->deadline_timer);
    }

    /*  Generate new survey ID. */
    ++surveyor->surveyid;

    /*  Tag the survey body with survey ID. */
    sp_assert (sp_chunkref_size (&msg->hdr) == 0);
    sp_chunkref_term (&msg->hdr);
    sp_chunkref_init (&msg->hdr, 4);
    sp_putl (sp_chunkref_data (&msg->hdr), surveyor->surveyid);

    /*  Send the survey. */
    rc = sp_xsurveyor_send (&surveyor->xsurveyor.sockbase, msg);
    errnum_assert (rc == 0, -rc);

    surveyor->flags |= SP_SURVEYOR_INPROGRESS;

    /*  Set up the re-send timer. */
    sp_timer_start (&surveyor->deadline_timer, surveyor->deadline);

    return 0;
}

static int sp_surveyor_recv (struct sp_sockbase *self, struct sp_msg *msg)
{
    int rc;
    struct sp_surveyor *surveyor;
    uint32_t surveyid;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    /*  If no survey is going on return EFSM error. */
    if (sp_slow (!(surveyor->flags & SP_SURVEYOR_INPROGRESS)))
       return -EFSM;

    while (1) {

        /*  Get next response. */
        rc = sp_xsurveyor_recv (&surveyor->xsurveyor.sockbase, msg);
        if (sp_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc == 0, -rc);

        /*  Get the survey ID. Ignore any stale responses. */
        if (sp_slow (sp_chunkref_size (&msg->hdr) != sizeof (uint32_t))) {
            sp_msg_term (msg);
            continue;
        }
        surveyid = sp_getl (sp_chunkref_data (&msg->hdr));
        if (sp_slow (surveyid != surveyor->surveyid)) {
            sp_msg_term (msg);
            continue;
        }
        sp_chunkref_term (&msg->hdr);
        sp_chunkref_init (&msg->hdr, 0);
        break;
    }

    return 0;
}

static void sp_surveyor_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, sink);

    /*  Cancel the survey. */
    surveyor->flags &= ~SP_SURVEYOR_INPROGRESS;

    /*  If there's a blocked recv() operation, unblock it. */
    sp_sockbase_unblock_recv (&surveyor->xsurveyor.sockbase);
}

static int sp_surveyor_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    if (level != SP_SURVEYOR)
        return -ENOPROTOOPT;

    if (option == SP_DEADLINE) {
        if (sp_slow (optvallen != sizeof (int)))
            return -EINVAL;
        surveyor->deadline = *(int*) optval;
        return 0;
    }

    return -ENOPROTOOPT;
}

static int sp_surveyor_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    if (level != SP_SURVEYOR)
        return -ENOPROTOOPT;

    if (option == SP_DEADLINE) {
        if (sp_slow (*optvallen < sizeof (int)))
            return -EINVAL;
        *(int*) optval = surveyor->deadline;
        *optvallen = sizeof (int);
        return 0;
    }

    return -ENOPROTOOPT;
}

static int sp_surveyor_sethdr (struct sp_msg *msg, const void *hdr,
    size_t hdrlen)
{
    if (sp_slow (hdrlen != 0))
       return -EINVAL;
    return 0;
}

static int sp_surveyor_gethdr (struct sp_msg *msg, void *hdr, size_t *hdrlen)
{
    *hdrlen = 0;
    return 0;
}

static struct sp_sockbase *sp_surveyor_create (int fd)
{
    struct sp_surveyor *self;

    self = sp_alloc (sizeof (struct sp_surveyor), "socket (surveyor)");
    alloc_assert (self);
    sp_surveyor_init (self, &sp_surveyor_sockbase_vfptr, fd);
    return &self->xsurveyor.sockbase;
}

static struct sp_socktype sp_surveyor_socktype_struct = {
    AF_SP,
    SP_SURVEYOR,
    sp_surveyor_create
};

struct sp_socktype *sp_surveyor_socktype = &sp_surveyor_socktype_struct;

