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

#ifndef SP_HAVE_WINDOWS
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

/*  Private functions. */
int sp_addr_parse_literal (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen);
void sp_addr_any (int flags, struct sockaddr_storage *result,
    sp_socklen *resultlen);

int sp_addr_parse_port (const char *addr, const char **colon)
{
    size_t pos;
    int port;
    
    /*  Find the last colon in the address. If there is no colon in the string
        or if the port contains non-numeric characters return error. */
    pos = strlen (addr);
    while (1) {
        if (!pos)
            return -EINVAL;
        --pos;
        if (addr [pos] == ':')
            break;
        if (addr [pos] < '0' || addr [pos] > '9')
            return -EINVAL;
    }

    /*  Return the pointer to the colon character. */
    if (colon)
        *colon = addr + pos;

    /*  Return the numeric value of the port. */
    port = atoi (addr + pos + 1);
    sp_assert (port >= 0);
    if (port > 0xffff)
        return -EINVAL;
    return port;
}

#if defined SP_HAVE_WINDOWS

int sp_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen)
{
    /*  Asterisk is a special name meaning "all interfaces". */
    if (addrlen == 1 && addr [0] == '*') {
        sp_addr_any (flags, result, resultlen);
        return 0;
    }

    /*  On Windows there are no sane network interface names. We'll treat the
        name as a IP address literal. */
    return sp_addr_parse_literal (addr, addrlen, flags, result, resultlen);
}

#elif defined SP_USE_IFADDRS

#include <ifaddrs.h>

int sp_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen)
{
    int rc;
    struct ifaddrs *ifaces;
    struct ifaddrs *it;
    struct ifaddrs *ipv4;
    struct ifaddrs *ipv6;
    size_t ifalen;

    /*  Asterisk is a special name meaning "all interfaces". */
    if (addrlen == 1 && addr [0] == '*') {
        sp_addr_any (flags, result, resultlen);
        return 0;
    }

    /*  Try to resolve the supplied string as a literal address. */
    rc = sp_addr_parse_literal (addr, addrlen, flags, result, resultlen);
    if (rc == 0)
        return 0;
    errnum_assert (rc == -EINVAL, -rc);

    /*  Get the list of local network interfaces from the system. */
    ifaces = NULL;
    rc = getifaddrs (&ifaces);
    errno_assert (rc == 0);
    sp_assert (ifaces);

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
            sp_assert (!ipv4);
            ipv4 = it;
            break;
        case AF_INET6:
            sp_assert (!ipv6);
            ipv6 = it;
            break;
        }
    }

    /*  IPv6 address is preferable. */
    if (ipv6 && !(flags & SP_ADDR_IPV4ONLY)) {
        result->ss_family = AF_INET;
        memcpy (result, ipv6->ifa_addr, sizeof (struct sockaddr_in6));
        *resultlen = sizeof (struct sockaddr_in6);
        freeifaddrs (ifaces);
        return 0;
    }

    /*  Use IPv4 address. */
    if (ipv4) {
        result->ss_family = AF_INET6;
        memcpy (result, ipv4->ifa_addr, sizeof (struct sockaddr_in));
        *resultlen = sizeof (struct sockaddr_in);
        freeifaddrs (ifaces);
        return 0;
    }

    /*  There's no such interface. */
    freeifaddrs (ifaces);
    return -ENODEV;
}

#endif

int sp_addr_parse_remote (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen)
{
    int rc;
    struct addrinfo query;
    struct addrinfo *reply;

    /*  Try to resolve the supplied string as a literal address. Note that
        in this case, there's no DNS lookup involved. */
    rc = sp_addr_parse_literal (addr, addrlen, flags, result, resultlen);
    if (rc == 0)
        return 0;
    errnum_assert (rc == -EINVAL, -rc);

    /*  The name is not a literal.*/
    memset (&query, 0, sizeof (query));
    if (flags & SP_ADDR_IPV4ONLY)
        query.ai_family = AF_INET;
    else {
        query.ai_family = AF_INET6;
#ifdef AI_V4MAPPED
        query.ai_flags = AI_V4MAPPED;
#endif
    }
    query.ai_socktype = SOCK_STREAM;

    /*  Perform the DNS lookup itself. */
    rc = getaddrinfo (addr, NULL, &query, &reply);
    if (rc)
        return -EFAULT;

    /*  Check that exactly one address is returned and return it to
        the caller. */
    sp_assert (reply && !reply->ai_next);
    memcpy (result, reply->ai_addr, reply->ai_addrlen);
    *resultlen = reply->ai_addrlen;

    freeaddrinfo (reply);
    
    return 0;
}

int sp_addr_parse_literal (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen)
{
    int rc;
    char addrz [INET6_ADDRSTRLEN > INET_ADDRSTRLEN ?
        INET6_ADDRSTRLEN :  INET_ADDRSTRLEN];

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
    if (!(flags & SP_ADDR_IPV4ONLY)) {
        rc = inet_pton (AF_INET6, addrz,
            &(((struct sockaddr_in6*) result)->sin6_addr));
        if (rc == 1) {
            result->ss_family = AF_INET6;
            *resultlen = sizeof (struct sockaddr_in6);
            return 0;
        }
        errno_assert (rc == 0);
    }

    /*  Try to interpret the literal as an IPv4 address. */
    rc = inet_pton (AF_INET, addrz,
        &(((struct sockaddr_in*) result)->sin_addr));
    if (rc == 1) {
        result->ss_family = AF_INET;
        *resultlen = sizeof (struct sockaddr_in);
        return 0;
    }
    errno_assert (rc == 0);

    /*  The supplied string is not a valid literal address. */
    return -EINVAL;
}

void sp_addr_any (int flags, struct sockaddr_storage *result,
    sp_socklen *resultlen)
{
    if (flags & SP_ADDR_IPV4ONLY) {
        result->ss_family = AF_INET;

        ((struct sockaddr_in*) result)->sin_addr.s_addr =
            htonl (INADDR_ANY);
        *resultlen = sizeof (struct sockaddr_in);
    }
    else {
        result->ss_family = AF_INET6;
        memcpy (&((struct sockaddr_in6*) result)->sin6_addr,
            &in6addr_any, sizeof (in6addr_any));
        *resultlen = sizeof (struct sockaddr_in6);
    }
}

