/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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
#include "btcp.h"
#include "ctcp.h"

#include "../../tcp.h"

#include "../utils/port.h"
#include "../utils/iface.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"

#include <string.h>

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#endif

/*  TCP-specific socket options. */

struct nn_tcp_optset {
    struct nn_optset base;
    int nodelay;
};

static void nn_tcp_optset_destroy (struct nn_optset *self);
static int nn_tcp_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen);
static int nn_tcp_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen);
static const struct nn_optset_vfptr nn_tcp_optset_vfptr = {
    nn_tcp_optset_destroy,
    nn_tcp_optset_setopt,
    nn_tcp_optset_getopt
};

/*  nn_transport interface. */
static int nn_tcp_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_tcp_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);
static struct nn_optset *nn_tcp_optset (void);

static struct nn_transport nn_tcp_vfptr = {
    "tcp",
    NN_TCP,
    NULL,
    NULL,
    nn_tcp_bind,
    nn_tcp_connect,
    nn_tcp_optset,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_tcp = &nn_tcp_vfptr;

static int nn_tcp_bind (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    const char *end;
    const char *pos;
    int port;
    struct sockaddr_storage ss;
    size_t sslen;

    /*  Parse the port. */
    end = addr + strlen (addr);
    pos = strrchr (addr, ':');
    nn_assert (pos);
    ++pos;
    rc = nn_port_resolve (pos, end - pos);
    if (rc < 0)
        return -EINVAL;
    port = rc;

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_iface_resolve (addr, pos - addr - 1, 1, &ss, &sslen);
    if (rc < 0)
        return -ENODEV;

    return nn_btcp_create (hint, epbase);
}

static int nn_tcp_connect (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    const char *end;
    const char *pos;
    int port;
    struct sockaddr_storage ss;
    size_t sslen;

    /*  Parse the port. */
    end = addr + strlen (addr);
    pos = strrchr (addr, ':');
    nn_assert (pos);
    ++pos;
    rc = nn_port_resolve (pos, end - pos);
    if (rc < 0)
        return -EINVAL;
    port = rc;

    /*  If local address is specified, check whether it is valid. */
    pos = strchr (addr, ';');
    if (pos) {
        /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
        rc = nn_iface_resolve (addr, pos - addr, 1, &ss, &sslen);
        if (rc < 0)
            return -ENODEV;
    }

    return nn_ctcp_create (hint, epbase);
}

static struct nn_optset *nn_tcp_optset ()
{
    struct nn_tcp_optset *optset;

    optset = nn_alloc (sizeof (struct nn_tcp_optset), "optset (tcp)");
    alloc_assert (optset);
    optset->base.vfptr = &nn_tcp_optset_vfptr;

    /*  Default values for TCP socket options. */
    optset->nodelay = 0;

    return &optset->base;   
}

static void nn_tcp_optset_destroy (struct nn_optset *self)
{
    struct nn_tcp_optset *optset;

    optset = nn_cont (self, struct nn_tcp_optset, base);
    nn_free (optset);
}

static int nn_tcp_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen)
{
    struct nn_tcp_optset *optset;
    int val;

    optset = nn_cont (self, struct nn_tcp_optset, base);

    /*  At this point we assume that all options are of type int. */
    if (optvallen != sizeof (int))
        return -EINVAL;
    val = *(int*) optval;

    switch (option) {
    case NN_TCP_NODELAY:
        if (nn_slow (val != 0 && val != 1))
            return -EINVAL;
        optset->nodelay = val;
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

static int nn_tcp_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen)
{
    struct nn_tcp_optset *optset;
    int intval;

    optset = nn_cont (self, struct nn_tcp_optset, base);

    switch (option) {
    case NN_TCP_NODELAY:
        intval = optset->nodelay;
        break;
    default:
        return -ENOPROTOOPT;
    }
    memcpy (optval, &intval,
        *optvallen < sizeof (int) ? *optvallen : sizeof (int));
    *optvallen = sizeof (int);
    return 0;
}

