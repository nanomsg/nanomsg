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

#include "tcpc.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

/*  Implementation of sp_epbase interface. */
static int sp_tcpc_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_tcpc_epbase_vfptr =
    {sp_tcpc_close};

int sp_tcpc_init (struct sp_tcpc *self, const char *addr, void *hint)
{
    sp_epbase_init (&self->epbase, &sp_tcpc_epbase_vfptr, hint);
    sp_assert (0);
    return 0;
}

static int sp_tcpc_close (struct sp_epbase *self, int linger)
{
    struct sp_tcpc *tcpc;

    tcpc = sp_cont (self, struct sp_tcpc, epbase);
    sp_assert (0);
    return 0;
}

