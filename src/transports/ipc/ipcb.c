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
#include "../../utils/alloc.h"

#include <string.h>
#include <sys/un.h>
#include <unistd.h>

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
    struct sockaddr_storage ss;
    socklen_t sslen;
    struct sockaddr_un *un;

    /*  Start in LISTENING state. */
    self->sink = &sp_ipcb_state_listening;

    /*  Delete the ipc file left over by the previous runs of
        the application. */
    rc = unlink (addr);
    errno_assert (rc == 0 || errno == ENOENT);

    /*  Create the AF_UNIX address. */
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    if (strlen (addr) >= sizeof (un->sun_path))
        return -ENAMETOOLONG;
    ss.ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));
    sslen = sizeof (struct sockaddr_un);

    sp_list_init (&self->ipcas);

    /*  Initialise the base class. */
    sp_epbase_init (&self->epbase, &sp_ipcb_epbase_vfptr, addr, hint);

    /*  Open the listening socket. */
    rc = sp_usock_init (&self->usock, &self->sink,
        AF_UNIX, SOCK_STREAM, 0, sp_epbase_getcp (&self->epbase));
    errnum_assert (rc == 0, -rc);
    /*  TODO:  Get the backlog value from the socket option! */
    rc = sp_usock_listen (&self->usock, (struct sockaddr*) &ss, sslen, 100);
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
    sp_ipca_init (ipca, &ipcb->epbase, s, usock, ipcb);
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

