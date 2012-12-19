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

#include "ipcb.h"
#include "ipca.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"

#include <string.h>

/*  Implementation of sp_epbase interface. */
static int sp_ipcb_close (struct sp_epbase *self, int linger);
static const struct sp_epbase_vfptr sp_ipcb_epbase_vfptr =
    {sp_ipcb_close};

/******************************************************************************/
/*  State: LISTENING                                                          */
/******************************************************************************/

static void sp_ipcb_listening_accepted (const struct sp_cp_sink **self,
    struct sp_usock *usock, int s);
static const struct sp_cp_sink sp_ipcb_state_listening = {
    NULL,
    NULL,
    NULL,
    sp_ipcb_listening_accepted,
    NULL,
    NULL
};

int sp_ipcb_init (struct sp_ipcb *self, const char *addr, void *hint)
{
    int rc;
    int port;
    const char *colon;
    struct sockaddr_storage ss;
    socklen_t sslen;

    /*  Start in LISTENING state. */
    self->sink = &sp_ipcb_state_listening;

    sp_list_init (&self->ipcas);

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
    sp_epbase_init (&self->epbase, &sp_ipcb_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = sp_usock_init (&self->usock, &self->sink,
        AF_INET, SOCK_STREAM, IPPROTO_TCP, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    rc = sp_usock_bind (&self->usock, (struct sockaddr*) &ss, sslen);
    errnum_assert (rc == 0, -rc);
    /*  TODO:  Get the backlog value from the socket option! */
    rc = sp_usock_listen (&self->usock, 100);
    errnum_assert (rc == 0, -rc);

    /*  Start waiting for incoming connection. */
    sp_usock_accept (&self->usock);

    return 0;
}

static void sp_ipcb_listening_accepted (const struct sp_cp_sink **self,
    struct sp_usock *usock, int s)
{
    struct sp_ipcb *ipcb;
    struct sp_ipca *ipca;

    ipcb = sp_cont (self, struct sp_ipcb, sink);

    ipca = sp_alloc (sizeof (struct sp_ipca), "ipca");
    alloc_assert (ipca);
    sp_ipca_init (ipca, &ipcb->epbase, s, usock);
    sp_list_insert (&ipcb->ipcas, &ipca->item, sp_list_end (&ipcb->ipcas));
}

/******************************************************************************/
/*  State: TERMINATING                                                        */
/******************************************************************************/

static void sp_ipcb_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock);
static const struct sp_cp_sink sp_ipcb_state_terminating = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    sp_ipcb_terminating_closed,
    NULL,
    NULL
};

static int sp_ipcb_close (struct sp_epbase *self, int linger)
{
    struct sp_ipcb *ipcb;
    struct sp_list_item *it;

    ipcb = sp_cont (self, struct sp_ipcb, epbase);

    /*  First, ask all the associated sessions to close. */
    for (it = sp_list_begin (&ipcb->ipcas); it != sp_list_end (&ipcb->ipcas);
          it = sp_list_next (&ipcb->ipcas, it))
        sp_ipca_close (sp_cont (it, struct sp_ipca, item));

    /*  Close the listening socket itself. */
    ipcb->sink = &sp_ipcb_state_terminating;
    sp_usock_close (&ipcb->usock);

    return 0;
}

static void sp_ipcb_terminating_closed (const struct sp_cp_sink **self,
    struct sp_usock *usock)
{
    struct sp_ipcb *ipcb;

    ipcb = sp_cont (self, struct sp_ipcb, sink);

    sp_epbase_term (&ipcb->epbase);
    sp_free (ipcb);
}

#endif

