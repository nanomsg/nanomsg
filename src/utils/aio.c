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

#include "aio.h"
#include "err.h"
#include "fast.h"
#include "cont.h"

#include <string.h>

struct sp_aio_ctx {
#if defined SP_HAVE_WINDOWS

    /*  Completion port to receive all the completion notifications on. */
    HANDLE cp;
#endif
};

/*  Singleton instance of sp_aio_ctx. */
static struct sp_aio_ctx self;

void sp_aio_init (void)
{
#if defined SP_HAVE_WINDOWS
    self.cp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    win_assert (self.cp);
#else
    sp_assert (0);
#endif
}

void sp_aio_term (void)
{
#if defined SP_HAVE_WINDOWS
    BOOL brc;

    /*  Close the completion port. The assumption is that all the associated
        sockets are already closed at this point. */
    brc = CloseHandle (self.cp);
    win_assert (brc);
#else
    sp_assert (0);
#endif
}

void sp_aio_register (struct sp_usock *usock)
{
    /*  On Windows, socket must be permanently associated with the completion
        port. On other platforms, do nothing. */
#if defined SP_HAVE_WINDOWS
    HANDLE h;

    h = CreateIoCompletionPort ((HANDLE) usock->s, self.cp, (ULONG_PTR) usock->s, 0);
    win_assert (h);
#endif
}

int sp_aio_send (struct sp_aio *aio, struct sp_usock *usock,
    const void *buf, size_t *len, int flags)
{
#if defined SP_HAVE_WINDOWS
    int rc;
    WSABUF sndbuf;
    DWORD nbytes;

    memset (&aio->io, 0, sizeof (aio->io));
    sndbuf.len = (u_long) *len;
    sndbuf.buf = (char FAR*) buf;
    rc = WSASend (usock->s, &sndbuf, 1, &nbytes, 0, &aio->io, NULL);
    if (sp_fast (rc == 0))
        return 0;
    wsa_assert (WSAGetLastError () == WSA_IO_PENDING);
    return -EINPROGRESS;
#else
    sp_assert (0);
#endif
}

int sp_aio_recv (struct sp_aio *aio, struct sp_usock *usock,
    void *buf, size_t *len, int flags)
{
#if defined SP_HAVE_WINDOWS
    int rc;
    WSABUF rcvbuf;
    DWORD nbytes;
    DWORD flgs;

    memset (&aio->io, 0, sizeof (aio->io));
    rcvbuf.len = (u_long) *len;
    rcvbuf.buf = (char FAR*) buf;
    flgs = (flags == SP_AIO_RECV_PARTIAL ? 0 : MSG_WAITALL);
    rc = WSARecv (usock->s, &rcvbuf, 1, &nbytes, &flgs, &aio->io, NULL);
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

int sp_aio_wait (struct sp_aio **aio, size_t *len, int timeout)
{
#if defined SP_HAVE_WINDOWS
    DWORD nbytes;
    ULONG_PTR key;
    OVERLAPPED *pio;
    BOOL brc;

    brc = GetQueuedCompletionStatus (self.cp, &nbytes, &key, &pio, timeout);
    if (sp_slow (brc == FALSE && pio == NULL))
        return -ETIMEDOUT;
    *aio = sp_cont (pio, struct sp_aio, io);
    *len = (size_t) nbytes;
    return 0;
#else
    sp_assert (0);
#endif
}

