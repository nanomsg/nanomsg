/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.
    Copyright 2022 Staysail Systems, Inc.

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

#include "literal.h"

#include "../../utils/err.h"
#include "../../utils/fast.h"

#include <string.h>

#ifndef NN_HAVE_WINDOWS
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include <winsock.h>
#endif

void nn_literal_link_local_resolve(struct in6_addr *in6addr, int64_t *sin6_scope_id, char *addr)
{
    if (! IN6_IS_ADDR_LINKLOCAL(in6addr)) {
        return;
    }
    struct addrinfo hints, *res;
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = 0;
    hints.ai_flags = AI_NUMERICHOST;
    int rc = getaddrinfo(addr, NULL, &hints, &res);
    if (0 == rc && res) {
        struct sockaddr_in6 * curr = ((struct sockaddr_in6 *) res->ai_addr);
        // set scope_id
        *sin6_scope_id = (int64_t) curr->sin6_scope_id;
    }
}

int nn_literal_resolve (const char *addr, size_t addrlen,
    int ipv4only, struct sockaddr_storage *result, size_t *resultlen)
{
    int rc;
    char addrz [INET6_ADDRSTRLEN > INET_ADDRSTRLEN ?
        INET6_ADDRSTRLEN :  INET_ADDRSTRLEN];
    struct in_addr inaddr;
    struct in6_addr in6addr;
    int64_t sin6_scope_id = -1;
    char *ifname_dindex = NULL;     // pointer to ifname's delimiter, '%'

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
    if (!ipv4only) {
        // check if '%' exists in addrz (i.e. '$addr%$ifname')
        ifname_dindex = strstr(addrz, "%");
        if (NULL == ifname_dindex) {
            rc = inet_pton (AF_INET6, addrz, &in6addr);
        } else {
            // set addrz[index] = '\0', so that inet_pton won't fail
            *ifname_dindex = '\0';
            rc = inet_pton (AF_INET6, addrz, &in6addr);
            // re-add the '%' so we'll be able to get the scope_id for link-local IPv6
            *ifname_dindex = '%';
        }
        if (1 == rc) {
            if (result) {
                nn_literal_link_local_resolve(&in6addr, &sin6_scope_id, addrz);
                result->ss_family = AF_INET6;
                ((struct sockaddr_in6*) result)->sin6_addr = in6addr;
                if (-1 < sin6_scope_id) {
                    ((struct sockaddr_in6*) result)->sin6_scope_id = (uint32_t) sin6_scope_id;
                }
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

