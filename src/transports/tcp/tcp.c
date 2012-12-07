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

#include "tcp.h"
#include "tcpb.h"
#include "tcpc.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"

/*  sp_transport interface. */
static const char *sp_tcp_name (void);
static void sp_tcp_init (void);
static void sp_tcp_term (void);
static int sp_tcp_bind (const char *addr, void *hint,
    struct sp_epbase **epbase);
static int sp_tcp_connect (const char *addr, void *hint,
    struct sp_epbase **epbase);

static struct sp_transport sp_tcp_vfptr = {
    sp_tcp_name,
    sp_tcp_init,
    sp_tcp_term,
    sp_tcp_bind,
    sp_tcp_connect
};

struct sp_transport *sp_tcp = &sp_tcp_vfptr;

static const char *sp_tcp_name (void)
{
    return "tcp";
}

static void sp_tcp_init (void)
{
}

static void sp_tcp_term (void)
{
}

static int sp_tcp_bind (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_tcpb *tcpb;

    tcpb = sp_alloc (sizeof (struct sp_tcpb), "tcpb");
    alloc_assert (tcpb);
    rc = sp_tcpb_init (tcpb, addr, hint);
    if (sp_slow (rc != 0)) {
        sp_free (tcpb);
        return rc;
    }

    return 0;
}

static int sp_tcp_connect (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_tcpc *tcpc;

    tcpc = sp_alloc (sizeof (struct sp_tcpc), "tcpc");
    alloc_assert (tcpc);
    rc = sp_tcpc_init (tcpc, addr, hint);
    if (sp_slow (rc != 0)) {
        sp_free (tcpc);
        return rc;
    }

    return 0;
}


