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

#include "../../tcp.h"

#include "../../utils/err.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/cont.h"
#include "../../utils/bstream.h"
#include "../../utils/cstream.h"
#include "../../utils/list.h"

#include <string.h>

#define NN_TCP_BACKLOG 100

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

/*  Private functions. */
static int nn_tcp_binit (const char *addr, struct nn_usock *usock,
    struct nn_cp *cp, int backlog);
static int nn_tcp_csockinit (struct nn_usock *usock, int sndbuf, int rcvbuf,
    struct nn_cp *cp);
static int nn_tcp_cresolve (const char *addr, struct sockaddr_storage *local,
    socklen_t *locallen, struct sockaddr_storage *remote, socklen_t *remotelen);

/*  nn_transport interface. */
static void nn_tcp_init (void);
static void nn_tcp_term (void);
static int nn_tcp_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_tcp_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);
static struct nn_optset *nn_tcp_optset ();

static struct nn_transport nn_tcp_vfptr = {
    "tcp",
    NN_TCP,
    nn_tcp_init,
    nn_tcp_term,
    nn_tcp_bind,
    nn_tcp_connect,
    nn_tcp_optset,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_tcp = &nn_tcp_vfptr;

static void nn_tcp_init (void)
{
}

static void nn_tcp_term (void)
{
}

static int nn_tcp_bind (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_bstream *bstream;

    bstream = nn_alloc (sizeof (struct nn_bstream), "bstream (tcp)");
    alloc_assert (bstream);
    rc = nn_bstream_init (bstream, addr, hint, nn_tcp_binit, NN_TCP_BACKLOG);
    if (nn_slow (rc != 0)) {
        nn_free (bstream);
        return rc;
    }
    *epbase = &bstream->epbase;

    return 0;
}

static int nn_tcp_connect (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    const char *end;
    const char *pos;
    struct nn_cstream *cstream;

    /*  Check the syntax of the address here. First, check whether port number
        is OK.  */
    end = addr + strlen (addr);
    pos = strrchr (addr, ':');
    if (!pos)
        return -EINVAL;
    ++pos;
    rc = nn_addr_parse_port (pos, end - pos);
    if (rc < 0)
        return rc;

    /*  Now check whether local address, in any, is valid. */
    pos = strchr (addr, ';');
    if (pos) {
        rc = nn_addr_parse_local (addr, pos - addr, NN_ADDR_IPV4ONLY,
            NULL, NULL);
        if (rc < 0)
            return rc;
    }

    /*  Create the async object to handle the connection. */
    cstream = nn_alloc (sizeof (struct nn_cstream), "cstream (tcp)");
    alloc_assert (cstream);
    rc = nn_cstream_init (cstream, addr, hint, nn_tcp_csockinit,
        nn_tcp_cresolve);
    if (nn_slow (rc != 0)) {
        nn_free (cstream);
        return rc;
    }
    *epbase = &cstream->epbase;

    return 0;
}

static int nn_tcp_binit (const char *addr, struct nn_usock *usock,
    struct nn_cp *cp, int backlog)
{
    int rc;
    int port;
    const char *end;
    const char *pos;
    struct sockaddr_storage ss;
    socklen_t sslen;

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (&ss, 0, sizeof (ss));

    /*  Parse the port. */
    end = addr + strlen (addr);
    pos = strrchr (addr, ':');
    if (!pos)
        return -EINVAL;
    ++pos;
    rc = nn_addr_parse_port (pos, end - pos);
    if (rc < 0)
        return rc;
    port = rc;

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_addr_parse_local (addr, pos - addr - 1, NN_ADDR_IPV4ONLY,
        &ss, &sslen);
    if (rc < 0)
        return rc;

    /*  Combine the port and the address. */
    if (ss.ss_family == AF_INET)
        ((struct sockaddr_in*) &ss)->sin_port = htons (port);
    else if (ss.ss_family == AF_INET6)
        ((struct sockaddr_in6*) &ss)->sin6_port = htons (port);
    else
        nn_assert (0);

    /*  Open the listening socket. */
    rc = nn_usock_init (usock, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP,
        -1, -1, cp);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_bind (usock, (struct sockaddr*) &ss, sslen);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_listen (usock, NN_TCP_BACKLOG);
    errnum_assert (rc == 0, -rc);

    return 0;
}

static int nn_tcp_csockinit (struct nn_usock *usock, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
    return nn_usock_init (usock, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP,
        sndbuf, rcvbuf, cp);
}

static int nn_tcp_cresolve (const char *addr, struct sockaddr_storage *local,
    socklen_t *locallen, struct sockaddr_storage *remote, socklen_t *remotelen)
{
    int rc;
    int port;
    const char *end;
    const char *colon;
    const char *semicolon;
    int res;

    res = 0;

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (remote, 0, sizeof (struct sockaddr_storage));

    /*  Parse the port. */
    end = addr + strlen (addr);
    colon = strrchr (addr, ':');
    port = nn_addr_parse_port (colon + 1, end - colon - 1);
    if (nn_slow (port == -EINVAL))
        return -EINVAL;
    errnum_assert (port > 0, -port);

    /*  Parse the local address, if any. */
    semicolon = strchr (addr, ';');
    if (semicolon) {
        memset (local, 0, sizeof (struct sockaddr_storage));
        rc = nn_addr_parse_local (addr, semicolon - addr, NN_ADDR_IPV4ONLY,
            local, locallen);
        errnum_assert (rc == 0, -rc);
        addr = semicolon + 1;
        res |= NN_CSTREAM_DOBIND;
    }

    /*  Parse the remote address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_addr_parse_remote (addr, colon - addr, NN_ADDR_IPV4ONLY,
        remote, remotelen);
    if (nn_slow (rc < 0))
        return rc;

    /*  Combine the port and the address. */
    if (remote->ss_family == AF_INET)
        ((struct sockaddr_in*) remote)->sin_port = htons (port);
    else if (remote->ss_family == AF_INET6)
        ((struct sockaddr_in6*) remote)->sin6_port = htons (port);
    else
        nn_assert (0);

    return res;
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

