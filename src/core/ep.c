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

void nn_epbase_init (struct nn_epbase *self,
    const struct nn_epbase_vfptr *vfptr, const char *addr, void *hint)
{

    /*  Set up the virtual functions table. */
    self->vfptr = vfptr;

    /*  Remember which socket the endpoint belongs to. */
    self->sock = (struct nn_sock*) hint;

    /*  This enpoint does not belong to any socket yet. */
    nn_list_item_init (&self->item);

    /*  Store the textual for of the address. */
    nn_assert (strlen (addr) <= NN_SOCKADDR_MAX);
#if defined _MSC_VER
#pragma warning (push)
#pragma warning (disable:4996)
#endif
    strcpy (self->addr, addr);
#if defined _MSC_VER
#pragma warning (pop)
#endif
}

void nn_epbase_term (struct nn_epbase *self)
{
    /*  If we are shutting the endpoint down before it was added to the socket
        (failure during initialisation), do nothing. */
    if (nn_slow (!nn_list_item_isinlist (&self->item)))
        return;

    nn_sock_ep_closed (self->sock, self);
    nn_list_item_term (&self->item);
}

struct nn_cp *nn_epbase_getcp (struct nn_epbase *self)
{
    return nn_sock_getcp (self->sock);
}

struct nn_worker *nn_epbase_choose_worker (struct nn_epbase *self)
{
    return nn_sock_choose_worker (self->sock);
}

const char *nn_epbase_getaddr (struct nn_epbase *self)
{
    return self->addr;
}

void nn_epbase_getopt (struct nn_epbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;

    rc = nn_sock_getopt (self->sock, level, option, optval, optvallen, 1);
    errnum_assert (rc == 0, -rc);
}

int nn_epbase_ispeer (struct nn_epbase *self, int socktype)
{
    return nn_sock_ispeer (self->sock, socktype);
}

int nn_ep_close (struct nn_ep *self)
{
    struct nn_epbase *epbase;

    epbase = (struct nn_epbase*) self;
    return epbase->vfptr->close (epbase);
}

