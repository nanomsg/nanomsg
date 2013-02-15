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

#include "../../utils/err.h"
#include "../../utils/addr.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/bstream.h"
#include "../../utils/cstream.h"

#define NN_TCP_BACKLOG 100

/*  Private functions. */
static int nn_tcp_binit (const char *addr, struct nn_usock *usock,
    struct nn_cp *cp, int backlog);
static int nn_tcp_csockinit (struct nn_usock *usock, int sndbuf, int rcvbuf,
    struct nn_cp *cp);
static int nn_tcp_cresolve (const char *addr, struct sockaddr_storage *ss,
    socklen_t *sslen);

/*  nn_transport interface. */
static void nn_tcp_init (void);
static void nn_tcp_term (void);
static int nn_tcp_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_tcp_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);

static struct nn_transport nn_tcp_vfptr = {
    "tcp",
    nn_tcp_init,
    nn_tcp_term,
    nn_tcp_bind,
    nn_tcp_connect
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
    struct nn_cstream *cstream;

    /*  TODO: Check the syntax of the address here! */

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
    const char *colon;
    struct sockaddr_storage ss;
    socklen_t sslen;

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (&ss, 0, sizeof (ss));

    /*  Parse the port. */
    rc = nn_addr_parse_port (addr, &colon);
    if (rc < 0)
        return rc;
    port = rc;

    /*  Parse the address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_addr_parse_local (addr, colon - addr, NN_ADDR_IPV4ONLY,
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
    rc = nn_usock_listen (usock, (struct sockaddr*) &ss, sslen, NN_TCP_BACKLOG);
    errnum_assert (rc == 0, -rc);

    return 0;
}

static int nn_tcp_csockinit (struct nn_usock *usock, int sndbuf, int rcvbuf,
    struct nn_cp *cp)
{
    return nn_usock_init (usock, NULL, AF_INET, SOCK_STREAM, IPPROTO_TCP,
        sndbuf, rcvbuf, cp);
}

static int nn_tcp_cresolve (const char *addr, struct sockaddr_storage *ss,
    socklen_t *sslen)
{
    int rc;
    int port;
    const char *colon;

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (ss, 0, sizeof (struct sockaddr_storage));

    /*  Parse the port. */
    port = nn_addr_parse_port (addr, &colon);
    errnum_assert (port > 0, -port);

    /*  TODO: Parse the local address, if any. */

    /*  Parse the remote address. */
    /*  TODO:  Get the actual value of the IPV4ONLY socket option. */
    rc = nn_addr_parse_remote (addr, colon - addr, NN_ADDR_IPV4ONLY,
        ss, sslen);
    if (nn_slow (rc < 0))
        return rc;

    /*  Combine the port and the address. */
    if (ss->ss_family == AF_INET)
        ((struct sockaddr_in*) ss)->sin_port = htons (port);
    else if (ss->ss_family == AF_INET6)
        ((struct sockaddr_in6*) ss)->sin6_port = htons (port);
    else
        nn_assert (0);

    return 0;
}

