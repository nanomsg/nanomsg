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

#include "../transport.h"

#include "ep.h"
#include "sock.h"

#include "../utils/err.h"

#include <string.h>

int nn_ep_init (struct nn_ep *self, struct nn_sock *sock, int eid,
    struct nn_transport *transport, int bind, const char *addr)
{
    int rc;

    self->epbase = NULL;
    self->sock = sock;
    self->eid = eid;
    nn_list_item_init (&self->item);

    /*  Store the textual form of the address. */
    nn_assert (strlen (addr) <= NN_SOCKADDR_MAX);
#if defined _MSC_VER
#pragma warning (push)
#pragma warning (disable:4996)
#endif
    strcpy (self->addr, addr);
#if defined _MSC_VER
#pragma warning (pop)
#endif

    /*  Create transport-specific part of the endpoint. */
    if (bind)
        rc = transport->bind ((void*) self, &self->epbase);
    else
        rc = transport->connect ((void*) self, &self->epbase);

    /*  Endpoint creation failed. */
    if (rc < 0) {
        nn_list_item_term (&self->item);
        return rc;
    }

    return 0;
}

void nn_ep_term (struct nn_ep *self)
{
    self->epbase->vfptr->destroy (self->epbase);
    nn_list_item_term (&self->item);
}

void nn_ep_stop (struct nn_ep *self)
{
    /*  Perform transport-specific shutdown procedure. */
    self->epbase->vfptr->stop (self->epbase);
}

struct nn_ctx *nn_ep_getctx (struct nn_ep *self)
{
    return nn_sock_getctx (self->sock);
}

const char *nn_ep_getaddr (struct nn_ep *self)
{
    return self->addr;
}

void nn_ep_getopt (struct nn_ep *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;

    rc = nn_sock_getopt_inner (self->sock, level, option, optval, optvallen);
    errnum_assert (rc == 0, -rc);
}

int nn_ep_ispeer (struct nn_ep *self, int socktype)
{
    return nn_sock_ispeer (self->sock, socktype);
}

