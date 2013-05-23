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

#include "dns.h"
#include "literal.h"

#include "../../utils/err.h"

#include <string.h>

#ifndef NN_HAVE_WINDOWS
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

int nn_addr_parse_remote (const char *addr, size_t addrlen, int ipv4only,
    struct sockaddr_storage *result, size_t *resultlen)
{
    int rc;
    struct addrinfo query;
    struct addrinfo *reply;
    char hostname [NN_SOCKADDR_MAX];

    /*  Try to resolve the supplied string as a literal address. Note that
        in this case, there's no DNS lookup involved. */
    rc = nn_literal_parse (addr, addrlen, ipv4only, result, resultlen);
    if (rc == 0)
        return 0;
    errnum_assert (rc == -EINVAL, -rc);

    /*  The name is not a literal.*/
    memset (&query, 0, sizeof (query));
    if (ipv4only)
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

