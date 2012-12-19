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

#include "ipc.h"
#include "ipcb.h"
#include "ipcc.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"

/*  sp_transport interface. */
static const char *sp_ipc_name (void);
static void sp_ipc_init (void);
static void sp_ipc_term (void);
static int sp_ipc_bind (const char *addr, void *hint,
    struct sp_epbase **epbase);
static int sp_ipc_connect (const char *addr, void *hint,
    struct sp_epbase **epbase);

static struct sp_transport sp_ipc_vfptr = {
    sp_ipc_name,
    sp_ipc_init,
    sp_ipc_term,
    sp_ipc_bind,
    sp_ipc_connect
};

struct sp_transport *sp_ipc = &sp_ipc_vfptr;

static const char *sp_ipc_name (void)
{
    return "ipc";
}

static void sp_ipc_init (void)
{
}

static void sp_ipc_term (void)
{
}

static int sp_ipc_bind (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_ipcb *ipcb;

    ipcb = sp_alloc (sizeof (struct sp_ipcb), "ipcb");
    alloc_assert (ipcb);
    rc = sp_ipcb_init (ipcb, addr, hint);
    if (sp_slow (rc != 0)) {
        sp_free (ipcb);
        return rc;
    }
    *epbase = &ipcb->epbase;

    return 0;
}

static int sp_ipc_connect (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_ipcc *ipcc;

    ipcc = sp_alloc (sizeof (struct sp_ipcc), "ipcc");
    alloc_assert (ipcc);
    rc = sp_ipcc_init (ipcc, addr, hint);
    if (sp_slow (rc != 0)) {
        sp_free (ipcc);
        return rc;
    }
    *epbase = &ipcc->epbase;

    return 0;
}

#endif

