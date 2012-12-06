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

#include "surveyor.h"
#include "xsurveyor.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"

#define SP_SURVEYOR_DEFAULT_DEADLINE_IVL 1000

#define SP_SURVEYOR_INPROGRESS 1

struct sp_surveyor {
    struct sp_xsurveyor xsurveyor;
    const struct sp_cp_sink *sink;
    uint32_t flags;
    uint32_t surveyid;
    int deadline_ivl;
    struct sp_timer deadline_timer;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_surveyor_term (struct sp_sockbase *self);
static int sp_surveyor_send (struct sp_sockbase *self, const void *buf,
    size_t len);
static int sp_surveyor_recv (struct sp_sockbase *self, void *buf, size_t *len);
static const struct sp_sockbase_vfptr sp_surveyor_sockbase_vfptr = {
    sp_surveyor_term,
    sp_xsurveyor_add,
    sp_xsurveyor_rm,
    sp_xsurveyor_in,
    sp_xsurveyor_out,
    sp_surveyor_send,
    sp_surveyor_recv,
    sp_xsurveyor_setopt,
    sp_xsurveyor_getopt
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

    self->deadline_ivl = SP_SURVEYOR_DEFAULT_DEADLINE_IVL;
    sp_timer_init (&self->deadline_timer, &self->sink,
        sp_sockbase_getcp (&self->xsurveyor.sockbase));
}

void sp_surveyor_term (struct sp_sockbase *self)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    sp_timer_term (&surveyor->deadline_timer);
    sp_xsurveyor_term (&surveyor->xsurveyor.sockbase);
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
    survey = sp_alloc (surveylen);
    alloc_assert (survey);
    sp_putl (survey, surveyor->surveyid);
    memcpy (((uint32_t*) survey) + 1, buf, len);

    /*  Send the survey. */
    rc = sp_xsurveyor_send (&surveyor->xsurveyor.sockbase, survey, surveylen);
    sp_free (survey);
    if (sp_slow (rc < 0))
        return rc;

    surveyor->flags |= SP_SURVEYOR_INPROGRESS;

    /*  Set up the re-send timer. */
    sp_timer_start (&surveyor->deadline_timer, surveyor->deadline_ivl);

    return 0;
}

static int sp_surveyor_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, xsurveyor.sockbase);

    /*  If no survey is going on return EFSM error. */
    if (sp_slow (!(surveyor->flags & SP_SURVEYOR_INPROGRESS)))
       return -EFSM;

    sp_assert (0);
}

static void sp_surveyor_timeout (const struct sp_cp_sink **self,
    struct sp_timer *timer)
{
    struct sp_surveyor *surveyor;

    surveyor = sp_cont (self, struct sp_surveyor, sink);

    sp_assert (0);
}

static struct sp_sockbase *sp_surveyor_create (int fd)
{
    struct sp_surveyor *self;

    self = sp_alloc (sizeof (struct sp_surveyor));
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

