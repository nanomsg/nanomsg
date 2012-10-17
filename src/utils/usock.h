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

#ifndef SP_USOCK_INCLUDED
#define SP_USOCK_INCLUDED

/*  Platform independent underlying (OS-level) socket object. */

#include "addr.h"

#if defined SP_HAVE_WINDOWS
#include "win.h"
#endif

struct sp_usock {
#if defined SP_HAVE_WINDOWS
    SOCKET s;
#else
    int s;
#endif
};

/*  The underlying socket is opened and tuned for the best performance. It is
    also opened in non-blocking mode. */
int sp_usock_init (struct sp_usock *self, int domain, int type, int protocol);
void sp_usock_term (struct sp_usock *self);

int sp_usock_bind (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);
int sp_usock_connect (struct sp_usock *self, const struct sockaddr *addr,
    sp_socklen addrlen);

#endif

