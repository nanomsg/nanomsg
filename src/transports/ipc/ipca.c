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

#if !defined SP_HAVE_WINDOWS

#include "ipca.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"

/******************************************************************************/
/*  State: CONNECTED                                                          */
/******************************************************************************/

/*  In this state control is yielded to the tcps state machine. */

static const struct sp_cp_sink sp_ipca_state_connected = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

void sp_ipca_init (struct sp_ipca *self, struct sp_epbase *epbase,
    int s, struct sp_usock *usock)
{
    self->sink = &sp_ipca_state_connected;
    sp_usock_init_child (&self->usock, usock, s, &self->sink, usock->cp);
    sp_tcps_init (&self->session, epbase, &self->usock);
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_ipca_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_ipca_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_ipca_terminating_closed,
    NULL,
    NULL
};

int sp_ipca_close (struct sp_ipca *self)
{
    /*  Terminate the associated session. */
    sp_assert (self->sink == &sp_ipca_state_connected);
    sp_tcps_term (&self->session);

    /*  Ask the underlying socket to terminate. */
    self->sink = &sp_ipca_state_terminating;
    sp_usock_close (&self->usock);

    return 0;
}

static void sp_ipca_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_ipca *ipca;

    ipca = sp_cont (self, struct sp_ipca, sink);

    sp_free (ipca);
}

#endif

