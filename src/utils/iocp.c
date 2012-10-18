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

#include "iocp.h"
#include "usock.h"
#include "err.h"
#include "fast.h"
#include "cont.h"

#include <string.h>

void sp_iocp_init (struct sp_iocp *self)
{
#if defined SP_HAVE_WINDOWS
    self->cp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self->cp);
#else
    sp_assert (0);
#endif
}

void sp_iocp_term (struct sp_iocp *self)
{
    /*  Close the completion port. The assumption is that all the associated
        sockets are already closed at this point. */
#if defined SP_HAVE_WINDOWS
    BOOL brc;

    brc = CloseHandle (self->cp);
    win_assert (brc);
#else
    sp_assert (0);
#endif
}

void sp_iocp_register (struct sp_iocp *self, struct sp_usock *usock)
{
    /*  On Windows, association of socket with a completion port is done on the
        OS level. On other platforms we'll do it manually. */
#if defined SP_HAVE_WINDOWS
    HANDLE h;

    h = CreateIoCompletionPort ((HANDLE) usock->s, self->cp,
        (ULONG_PTR) usock->s, 0);
    win_assert (h);
#else
    usock->iocp = self;
#endif
}

int sp_iocp_wait (struct sp_iocp *self, int timeout,
    struct sp_iocp_task **task, size_t *len)
{
#if defined SP_HAVE_WINDOWS
    DWORD nbytes;
    ULONG_PTR key;
    OVERLAPPED *pio;
    BOOL brc;

    brc = GetQueuedCompletionStatus (self->cp, &nbytes, &key, &pio, timeout);
    if (sp_slow (brc == FALSE && pio == NULL))
        return -ETIMEDOUT;
    *task = sp_cont (pio, struct sp_iocp_task, io);
    *len = (size_t) nbytes;
    return 0;
#else
    sp_assert (0);
#endif
}

int sp_usock_send (struct sp_usock *self, const void *buf, size_t *len,
    int flags, struct sp_iocp_task *task)
{
#if defined SP_HAVE_WINDOWS
    int rc;
    WSABUF sndbuf;
    DWORD nbytes;

    memset (&task->io, 0, sizeof (task->io));
    sndbuf.len = (u_long) *len;
    sndbuf.buf = (char FAR*) buf;
    rc = WSASend (self->s, &sndbuf, 1, &nbytes, 0, &task->io, NULL);
    if (sp_fast (rc == 0))
        return 0;
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
    return -EINPROGRESS;
#else
    sp_assert (0);
#endif
}

int sp_usock_recv (struct sp_usock *self, void *buf, size_t *len,
    int flags, struct sp_iocp_task *task)
{
#if defined SP_HAVE_WINDOWS
    int rc;
    WSABUF rcvbuf;
    DWORD nbytes;
    DWORD flgs;

    memset (&task->io, 0, sizeof (task->io));
    rcvbuf.len = (u_long) *len;
    rcvbuf.buf = (char FAR*) buf;
    flgs = (flags == SP_USOCK_PARTIAL ? 0 : MSG_WAITALL);
    rc = WSARecv (self->s, &rcvbuf, 1, &nbytes, &flgs, &task->io, NULL);
    if (sp_fast (rc == 0)) {
        *len = (size_t) nbytes;
        return 0;
    }
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
    return -EINPROGRESS;
#else
    sp_assert (0);
#endif
}

