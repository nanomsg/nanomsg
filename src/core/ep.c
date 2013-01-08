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

void sp_epbase_init (struct sp_epbase *self,
    const struct sp_epbase_vfptr *vfptr, const char *addr, void *hint)
{
    /*  Set up the virtual functions table. */
    self->vfptr = vfptr;

    /*  Remember which socket the endpoint belongs to. */
    self->sock = (struct sp_sock*) hint;

    /*  Store the textual for of the address. */
    sp_assert (strlen (addr) <= SP_SOCKADDR_MAX);
#if defined _MSC_VER
#pragma warning (push)
#pragma warning (disable:4996)
#endif
    strcpy (self->addr, addr);
#if defined _MSC_VER
#pragma warning (pop)
#endif
}

void sp_epbase_term (struct sp_epbase *self)
{
    sp_sock_ep_closed (self->sock, self);
}

struct sp_cp *sp_epbase_getcp (struct sp_epbase *self)
{
    return sp_sock_getcp (self->sock);
}

const char *sp_epbase_getaddr (struct sp_epbase *self)
{
    return self->addr;
}

void sp_epbase_getopt (struct sp_epbase *self, int level, int option,
    void *optval, size_t *optvallen)
{
    int rc;

    rc = sp_sock_getopt (self->sock, level, option, optval, optvallen, 1);
    errnum_assert (rc == 0, -rc);
}

int sp_ep_fd (struct sp_ep *self)
{
    return sp_sock_fd (((struct sp_epbase*) self)->sock);
}

void sp_ep_close (struct sp_ep *self)
{
    struct sp_epbase *epbase;

    epbase = (struct sp_epbase*) self;
    epbase->vfptr->close (epbase);
}

