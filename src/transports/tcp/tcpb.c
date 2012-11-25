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

#include "tcpb.h"
#include "tcpa.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"

#include <string.h>

/*  Private functions. */
static void sp_tcpb_accept (struct sp_tcpb *self);

/*  Implementation of sp_epbase interface. */
static int sp_tcpb_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_tcpb_epbase_vfptr =
    {sp_tcpb_close};

/*  Sink for aio events. */
static void sp_tcpb_accepted (const struct sp_sink **self,
    struct sp_usock *usock, int s);
static const struct sp_sink sp_tcpb_sink = {
    NULL,
    NULL,
    NULL,
    sp_tcpb_accepted,
    NULL,
    NULL
};

int sp_tcpb_init (struct sp_tcpb *self, const char *addr, void *hint)
{
    int rc;
    int port;
    const char *colon;
    struct sockaddr_storage ss;
    socklen_t sslen;

    self->sink = &sp_tcpb_sink;
    sp_list_init (&self->tcpas);

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (&ss, 0, sizeof (ss));

    /*  Parse the port. */
    rc = sp_addr_parse_port (addr, &colon);
    if (rc < 0)
        return rc;
    port = rc;

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = sp_addr_parse_local (addr, colon - addr, SP_ADDR_IPV4ONLY,
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
    sp_epbase_init (&self->epbase, &sp_tcpb_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = sp_usock_init (&self->usock, &self->sink,
        AF_INET, SOCK_STREAM, IPPROTO_TCP, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    rc = sp_usock_bind (&self->usock, (struct sockaddr*) &ss, sslen);
    errnum_assert (rc == 0, -rc);
    /*  TODO:  Get the backlog value from the socket option! */
    rc = sp_usock_listen (&self->usock, 100);
    errnum_assert (rc == 0, -rc);

    /*  Accept any connections that may be available at the moment. */
    sp_tcpb_accept (self);

    return 0;
}

static int sp_tcpb_close (struct sp_epbase *self, int linger)
{
    struct sp_tcpb *tcpb;

    tcpb = sp_cont (self, struct sp_tcpb, epbase);

    sp_assert (0);
}

static void sp_tcpb_accept (struct sp_tcpb *self)
{
    int rc;

    while (1) {

        /*  Launch new accept request. */
        rc = sp_usock_accept (&self->usock);
        if (rc == -EINPROGRESS)
            break;
        errnum_assert (rc == 0, -rc);

        /*  TODO */
        sp_assert (0);
    }
}

static void sp_tcpb_accepted (const struct sp_sink **self,
    struct sp_usock *usock, int s)
{
    struct sp_tcpb *tcpb;
    struct sp_tcpa *tcpa;

    tcpb = sp_cont (self, struct sp_tcpb, sink);

    tcpa = sp_alloc (sizeof (struct sp_tcpa));
    alloc_assert (tcpa);
    sp_tcpa_init (tcpa, s, usock);
    sp_list_insert (&tcpb->tcpas, &tcpa->item, sp_list_end (&tcpb->tcpas));
}


