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

#ifndef SP_ADDR_INCLUDED
#define SP_ADDR_INCLUDED

#if defined SP_HAVE_WINDWS
#include "win.h"
typedef int sp_socklen;
#else
#include <sys/socket.h>
#include <netinet/in.h>
typedef socklen_t sp_socklen;
#endif

/*  Helper functions for parsing addresses in form "host:port" where host is
    either an IP address literal, a hostname or a network interface name and
    port is a numeric decimal value. */

#define SP_ADDR_IPV4ONLY 1

/*  Parse port numbers at the end of the address string. The function returns
    L4 port (e.g. TCP port). If successful, 'colon' output parameter will
    contain poiter to the colon character in the address string (delimiter
    between address and port part of the string). */
int sp_addr_parse_port (const char *addr, const char **colon);

/*  Resolves name of a local network interface into the address itself.
    Name '*' is resolved as 'all interfaces'. */
int sp_addr_parse_local (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen);

/*  Resolves name of a local address into the address itself.
    TODO:  This should be an asynchronous operation. This way a DNS failure
    can block the whole system. */
int sp_addr_parse_remote (const char *addr, size_t addrlen, int flags,
    struct sockaddr_storage *result, sp_socklen *resultlen);

#endif
