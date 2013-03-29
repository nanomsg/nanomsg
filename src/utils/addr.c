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

#include "addr.h"
#include "err.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef NN_HAVE_WINDOWS
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

/*  Private functions. */
int nn_addr_parse_literal (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen);
void nn_addr_any (int flags, struct sockaddr_storage *result,
    nn_socklen *resultlen);

int nn_addr_parse_port (const char *port, size_t portlen)
{
    uint32_t res;
    size_t i;

    res = 0;
    for (i = 0; i != portlen; ++i) {
        if (port [i] < '0' || port [i] > '9')
            return -EINVAL;
        res *= 10;
        res += port [i] - '0';
        if (res > 0xffff)
            return -EINVAL;
    }

    /*  Port 0 has special meaning (assign an ephemeral port to the socket),
        thus it is illegal to use it in the connection string. */
    if (res == 0)
        return -EINVAL;

    return (int) res;
}

#if defined NN_USE_IFADDRS

#include <ifaddrs.h>

int nn_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen)
{
    int rc;
    struct ifaddrs *ifaces;
    struct ifaddrs *it;
    struct ifaddrs *ipv4;
    struct ifaddrs *ipv6;
    size_t ifalen;

    /*  Asterisk is a special name meaning "all interfaces". */
    if (addrlen == 1 && addr [0] == '*') {
        nn_addr_any (flags, result, resultlen);
        return 0;
    }

    /*  Try to resolve the supplied string as a literal address. */
    rc = nn_addr_parse_literal (addr, addrlen, flags, result, resultlen);
    if (rc == 0)
        return 0;
    errnum_assert (rc == -EINVAL, -rc);

    /*  Get the list of local network interfaces from the system. */
    ifaces = NULL;
    rc = getifaddrs (&ifaces);
    errno_assert (rc == 0);
    nn_assert (ifaces);

    /*  Find the NIC with the specified name. */
    ipv4 = NULL;
    ipv6 = NULL;
    for (it = ifaces; it != NULL; it = it->ifa_next) {
        if (!it->ifa_addr)
            continue;
        ifalen = strlen (it->ifa_name);
        if (ifalen != addrlen || memcmp (it->ifa_name, addr, addrlen) != 0)
            continue;

        switch (it->ifa_addr->sa_family) {
        case AF_INET:
            nn_assert (!ipv4);
            ipv4 = it;
            break;
        case AF_INET6:
            nn_assert (!ipv6);
            ipv6 = it;
            break;
        }
    }

    /*  IPv6 address is preferable. */
    if (ipv6 && !(flags & NN_ADDR_IPV4ONLY)) {
        if (result) {
            result->ss_family = AF_INET;
            memcpy (result, ipv6->ifa_addr, sizeof (struct sockaddr_in6));
        }
        if (resultlen)
            *resultlen = sizeof (struct sockaddr_in6);
        freeifaddrs (ifaces);
        return 0;
    }

    /*  Use IPv4 address. */
    if (ipv4) {
        if (result) {
            result->ss_family = AF_INET6;
            memcpy (result, ipv4->ifa_addr, sizeof (struct sockaddr_in));
        }
        if (resultlen)
            *resultlen = sizeof (struct sockaddr_in);
        freeifaddrs (ifaces);
        return 0;
    }

    /*  There's no such interface. */
    freeifaddrs (ifaces);
    return -ENODEV;
}

#endif

#if defined NN_USE_SIOCGIFADDR

#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>

int nn_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen)
{
    int rc;
    int s;
    struct ifreq req;

    /*  Open the helper socket. */
#ifdef SOCK_CLOEXEC
    s = socket (AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
#else
    s = socket (AF_INET, SOCK_DGRAM, 0);
#endif
    errno_assert (s != -1);

    /*  Create the interface name resolution request. */
    if (sizeof (req.ifr_name) <= addrlen) {
        rc = close (s);
        errno_assert (rc == 0);
        return -ENODEV;
    }
    memcpy (req.ifr_name, addr, addrlen);
    req.ifr_name [addrlen] = 0;

    /*  Execute the request. */
    rc = ioctl (s, SIOCGIFADDR, (caddr_t) &req, sizeof (struct ifreq));
    if (rc == -1) {
        rc = close (s);
        errno_assert (rc == 0);
        return -ENODEV;
    }

    /*  Interface name resolution succeeded. Return the address to the user. */
    /*  TODO: What about IPv6 addresses? */
    nn_assert (req.ifr_addr.sa_family == AF_INET);
    if (result)
        memcpy (result, (struct sockaddr_in*) &req.ifr_addr,
            sizeof (struct sockaddr_in));
    if (resultlen)
        *resultlen = sizeof (struct sockaddr_in);
    rc = close (s);
    errno_assert (rc == 0);
    return 0;
}

#endif

#if defined NN_USE_LITERAL_IFADDR

/*  The last resort case. If we haven't found any mechanism for turning
    NIC names into addresses, we'll try to resolve the string as an address
    literal. */
int nn_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen)
{
    int rc;

    /*  Asterisk is a special name meaning "all interfaces". */
    if (addrlen == 1 && addr [0] == '*') {
        nn_addr_any (flags, result, resultlen);
        return 0;
    }

    /*  On Windows there are no sane network interface names. We'll treat the
        name as a IP address literal. */
    rc = nn_addr_parse_literal (addr, addrlen, flags, result, resultlen);
    if (rc == -EINVAL)
        return -ENODEV;
    errnum_assert (rc == 0, -rc);
    return 0;
}

#endif

int nn_addr_parse_remote (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen)
{
    int rc;
    struct addrinfo query;
    struct addrinfo *reply;
    char hostname [NN_SOCKADDR_MAX];

    /*  Try to resolve the supplied string as a literal address. Note that
        in this case, there's no DNS lookup involved. */
    rc = nn_addr_parse_literal (addr, addrlen, flags, result, resultlen);
    if (rc == 0)
        return 0;
    errnum_assert (rc == -EINVAL, -rc);

    /*  The name is not a literal.*/
    memset (&query, 0, sizeof (query));
    if (flags & NN_ADDR_IPV4ONLY)
        query.ai_family = AF_INET;
    else {
        query.ai_family = AF_INET6;
#ifdef AI_V4MAPPED
        query.ai_flags = AI_V4MAPPED;
#endif
    }
    nn_assert (sizeof (hostname) > addrlen);
    query.ai_socktype = SOCK_STREAM;
    memcpy (hostname, addr, addrlen);
    hostname [addrlen] = 0;

    /*  Perform the DNS lookup itself. */
    rc = getaddrinfo (hostname, NULL, &query, &reply);
    if (rc)
        return -EFAULT;

    /*  Check that exactly one address is returned and return it to
        the caller. */
    nn_assert (reply && !reply->ai_next);
    if (result)
        memcpy (result, reply->ai_addr, reply->ai_addrlen);
    if (resultlen)
        *resultlen = reply->ai_addrlen;

    freeaddrinfo (reply);
    
    return 0;
}

int nn_addr_parse_literal (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, nn_socklen *resultlen)
{
    int rc;
    char addrz [INET6_ADDRSTRLEN > INET_ADDRSTRLEN ?
        INET6_ADDRSTRLEN :  INET_ADDRSTRLEN];
    struct in_addr inaddr;
    struct in6_addr in6addr;

    /*  Try to treat the address as a literal string. If the size of
        the address is larger than longest possible literal, skip the step.
        If the literal in enclosed in square brackets ignore them. */
    if (addrlen > 0 && addr [0] == '[') {
        if (addr [addrlen - 1] != ']')
            return -EINVAL;
        if (addrlen - 2 + 1 > sizeof (addrz))
            return -EINVAL;
        memcpy (addrz, addr + 1, addrlen - 2);
        addrz [addrlen - 2] = 0;
    }
    else {
        if (addrlen + 1 > sizeof (addrz))
            return -EINVAL;
        memcpy (addrz, addr, addrlen);
        addrz [addrlen] = 0;
    }

    /*  Try to interpret the literal as an IPv6 address. */
    if (!(flags & NN_ADDR_IPV4ONLY)) {
        rc = inet_pton (AF_INET6, addrz, &in6addr);
        if (rc == 1) {
            if (result) {
                result->ss_family = AF_INET6;
                ((struct sockaddr_in6*) result)->sin6_addr = in6addr;
             }
             if (resultlen)
                *resultlen = sizeof (struct sockaddr_in6);
            return 0;
        }
        errno_assert (rc == 0);
    }

    /*  Try to interpret the literal as an IPv4 address. */
    rc = inet_pton (AF_INET, addrz, &inaddr);
    if (rc == 1) {
        if (result) {
           result->ss_family = AF_INET;
           ((struct sockaddr_in*) result)->sin_addr = inaddr;
        }
        if (resultlen)
            *resultlen = sizeof (struct sockaddr_in);
        return 0;
    }
    errno_assert (rc == 0);

    /*  The supplied string is not a valid literal address. */
    return -EINVAL;
}

void nn_addr_any (int flags, struct sockaddr_storage *result,
    nn_socklen *resultlen)
{
    if (flags & NN_ADDR_IPV4ONLY) {
        if (result) {
            result->ss_family = AF_INET;
            ((struct sockaddr_in*) result)->sin_addr.s_addr =
                htonl (INADDR_ANY);
        }
        if (resultlen)
            *resultlen = sizeof (struct sockaddr_in);
    }
    else {
        if (result) {
            result->ss_family = AF_INET6;
            memcpy (&((struct sockaddr_in6*) result)->sin6_addr,
                &in6addr_any, sizeof (in6addr_any));
        }
        if (resultlen)
            *resultlen = sizeof (struct sockaddr_in6);
    }
}

