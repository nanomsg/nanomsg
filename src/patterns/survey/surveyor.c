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
static int sp_surveyor_send (struct sp_sockbase *self, const void *buf,
    size_t len);
static int sp_surveyor_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_surveyor_setopt (struct sp_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int sp_surveyor_getopt (struct sp_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_surveyor_sockbase_vfptr = {
    sp_surveyor_destroy,
    sp_xsurveyor_add,
    sp_xsurveyor_rm,
    sp_xsurveyor_in,
    sp_xsurveyor_out,
    sp_surveyor_send,
    sp_surveyor_recv,
    sp_surveyor_setopt,
    sp_surveyor_getopt
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

static int sp_surveyor_send (struct sp_sockbase *self, const void *buf,
    size_t len)
{
    int rc;
    struct sp_surveyor *surveyor;
    size_t surveylen;
    void *survey;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    /*  Cancel any ongoing survey. */
    if (sp_slow (surveyor->flags & SP_SURVEYOR_INPROGRESS)) {
        surveyor->flags &= ~SP_SURVEYOR_INPROGRESS;
        sp_timer_stop (&surveyor->deadline_timer);
    }

    /*  Generate new survey ID. */
    ++surveyor->surveyid;

    /*  Tag the survey body with survey ID. */
    /*  TODO: Do this using iovecs. */
    surveylen = sizeof (uint32_t) + len;
    survey = sp_alloc (surveylen, "survey");
    alloc_assert (survey);
    sp_putl (survey, surveyor->surveyid);
    memcpy (((uint32_t*) survey) + 1, buf, len);

    /*  Send the survey. */
    rc = sp_xsurveyor_send (&surveyor->xsurveyor.sockbase, survey, surveylen);
    if (sp_slow (rc == -EAGAIN)) {
        sp_free (survey);
        return -EAGAIN;
    }
    errnum_assert (rc == 0, -rc);
    sp_free (survey);

    surveyor->flags |= SP_SURVEYOR_INPROGRESS;

    /*  Set up the re-send timer. */
    sp_timer_start (&surveyor->deadline_timer, surveyor->deadline);

    return 0;
}

static int sp_surveyor_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_surveyor *surveyor;
    size_t tmplen;
    uint8_t *tmpbuf;
    uint32_t surveyid;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    /*  If no survey is going on return EFSM error. */
    if (sp_slow (!(surveyor->flags & SP_SURVEYOR_INPROGRESS)))
       return -EFSM;

    /*  Prepare temporary buffer for incoming messages. */
    tmpbuf = sp_alloc (*len + 4, "response");
    alloc_assert (tmpbuf);

    while (1) {

        /*  Get next response. Split it into survey ID and the body. */
        tmplen = *len + 4;
        rc = sp_xsurveyor_recv (&surveyor->xsurveyor.sockbase,
            tmpbuf, &tmplen);
        if (sp_slow (rc == -EAGAIN)) {
            sp_free (tmpbuf);
            return -EAGAIN;
        }
        errnum_assert (rc == 0, -rc);
        surveyid = sp_getl (tmpbuf);

        /*  Ignore any stale responses. */
        if (surveyid == surveyor->surveyid)
            break;
    }

    memcpy (buf, tmpbuf + 4, tmplen - 4);
    *len = tmplen - 4;
    sp_free (tmpbuf);

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

