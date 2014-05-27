/*
    Copyright (c) 2013 250bpm s.r.o.  All rights reserved.

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

#include "usock.h"
#include "../transport.h"

#if defined NN_HAVE_WINDOWS
#include "../utils/win.h"
#else
#include <unistd.h>
#include <sys/un.h>
#endif

#if defined NN_HAVE_WINDOWS
#include "usock_win.inc"
#else
#include "usock_posix.inc"
#endif

int nn_usock_unlink(struct sockaddr_un *un)
{
#if defined NN_HAVE_WINDOWS
    if (un->sun_family == AF_UNIX) {
        //TODO:
        return 0;
    }
#endif
    return unlink(un->sun_path);
}

void nn_usock_create_ipc_address(struct nn_epbase *ep, struct sockaddr_storage *ss)
{
    struct sockaddr_un *un;
    const char *addr;

    addr = nn_epbase_getaddr(ep);
    memset(ss, 0, sizeof(ss[0]));
    un = (struct sockaddr_un*)ss;
    nn_assert(strlen(addr) < sizeof(un->sun_path));
    un->sun_family = AF_UNIX;
    strncpy(un->sun_path, addr, sizeof(un->sun_path));
}

int nn_usock_geterrno (struct nn_usock *self) {
    return self->errnum;
}
