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
#include "../../utils/addr.h"

/*  Implementation of sp_epbase interface. */
static int sp_tcpc_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_tcpc_epbase_vfptr =
    {sp_tcpc_close};

int sp_tcpc_init (struct sp_tcpc *self, const char *addr, void *hint)
{
    int rc;
    int port;
    const char *colon;
    struct sockaddr_storage ss;
    socklen_t sslen;

    /*  Parse the port. */
    rc = sp_addr_parse_port (addr, &colon);
    if (rc < 0)
        return rc;
    port = rc;

    /*  TODO: Parse the local address, if any. */

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = sp_addr_parse_remote (addr, colon - addr, SP_ADDR_IPV4ONLY,
        &ss, &sslen);
    if (rc < 0)
        return rc;

    /*  Combine the port and the address. */
    if (ss.ss_family == AF_INET)
        ((struct sockaddr_in*) &ss)->sin_port = htons (port);
    else if (ss.ss_family == AF_INET6)
        ((struct sockaddr_in6*) &ss)->sin6_port = htons (port);
    else
        sp_assert (0);

    /*  Initialise the base class. */
    sp_epbase_init (&self->epbase, &sp_tcpc_epbase_vfptr, hint);

    /*  TODO:  Open the socket and start connecting. */

    return 0;
}

static int sp_tcpc_close (struct sp_epbase *self, int linger)
{
    struct sp_tcpc *tcpc;

    tcpc = sp_cont (self, struct sp_tcpc, epbase);
    sp_assert (0);
    return 0;
}

