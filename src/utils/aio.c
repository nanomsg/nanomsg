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

#if !defined SP_HAVE_WINDOWS

#include "err.h"
#include "fast.h"
#include "cont.h"

#include <stdint.h>
#include <sys/socket.h>

void sp_aio_init (struct sp_aio *self)
{
    sp_poller_init (&self->poller);
}

void sp_aio_term (struct sp_aio *self)
{
    sp_poller_term (&self->poller);
}

void sp_aio_register (struct sp_aio *self, int fd, struct sp_aio_hndl *hndl)
{
    hndl->fd = fd;

    /*  Start polling on the file descriptor. */
    sp_poller_add_fd (&self->poller, fd, &hndl->hndl);

    /*  Mark that there's no in or out operation in progress. */
    hndl->in.flags = 0;
    hndl->out.flags = 0;
}

void sp_aio_unregister (struct sp_aio *self, struct sp_aio_hndl *hndl)
{
    sp_poller_rm_fd (&self->poller, &hndl->hndl);
}

void sp_aio_send (struct sp_aio *self, struct sp_aio_hndl *hndl,
    const void *buf, size_t len, int flags)
{
    /*  If there's out operation already in progress, fail. */
    sp_assert (!hndl->out.flags);

    /*  Store the info about the asynchronous operation requested. */
    hndl->out.flags = SP_AIO_IN_PROGRESS | flags;
    hndl->out.buf = buf;
    hndl->out.len = len;
    hndl->out.olen = len;

    /*  Start polling for out. */
    sp_poller_set_out (&self->poller, &hndl->hndl);
}

void sp_aio_recv (struct sp_aio *self, struct sp_aio_hndl *hndl,
    void *buf, size_t len, int flags)
{
    /*  If there's in operation already in progress, fail. */
    sp_assert (!hndl->in.flags);

    /*  Store the info about the asynchronous operation requested. */
    hndl->in.flags = SP_AIO_IN_PROGRESS | flags;
    hndl->in.buf = buf;
    hndl->in.len = len;
    hndl->in.olen = len;

    /*  Start polling for in. */
    sp_poller_set_in (&self->poller, &hndl->hndl);
}

void sp_aio_pollin (struct sp_aio *self, struct sp_aio_hndl *hndl)
{
    sp_aio_recv (self, hndl, NULL, 0, 0);
}

void sp_aio_pollout (struct sp_aio *self, struct sp_aio_hndl *hndl)
{
    sp_aio_send (self, hndl, NULL, 0, 0);
}

int sp_aio_wait (struct sp_aio *self, int timeout, struct sp_aio_hndl **hndl,
    int *event, size_t *len)
{
    int rc;
    int pevent;
    struct sp_poller_hndl *phndl;
    struct sp_aio_hndl *ahndl;
    ssize_t nbytes;

    /*  Get one event. */        
    rc = sp_poller_wait (&self->poller, timeout, &pevent, &phndl);
    if (sp_slow (rc < 0))
       return rc;
    ahndl = sp_cont (phndl, struct sp_aio_hndl, hndl);

    switch (pevent) {

    case SP_POLLER_IN:

        /*  Process inbound data. */
        if (ahndl->in.len <= 0)
            goto recv_done;

        /*  This operation doesn't copy arbitrary amount of data. The amount
            is bounded by the socket's RCVBUF. */
        nbytes = recv (ahndl->fd, ahndl->in.buf, ahndl->in.len, 0);
        errno_assert (nbytes >= 0);
        if (sp_slow (nbytes == 0))
            goto error;

        ahndl->in.buf = ((uint8_t*) ahndl->in.buf) + nbytes;
        ahndl->in.len -= nbytes;
        if (ahndl->in.len > 0 && !(ahndl->in.flags & SP_AIO_PARTIAL))
            return -ETIMEDOUT;
recv_done:
        ahndl->in.flags = 0;
        *hndl = ahndl;
        *event = SP_AIO_IN;
        *len = ahndl->in.olen - ahndl->in.len;
        return 0;

    case SP_POLLER_OUT:

        /* Process outbound data. */
        if (ahndl->out.len <= 0)
            goto send_done;

        /*  This operation doesn't copy arbitrary amount of data. The amount
            is bounded by the socket's SNDBUF. */
        nbytes = send (ahndl->fd, ahndl->in.buf, ahndl->in.len, 0);
        errno_assert (nbytes >= 0);

        ahndl->out.buf = ((uint8_t*) ahndl->out.buf) + nbytes;
        ahndl->out.len -= nbytes;
        if (ahndl->out.len > 0 && !(ahndl->out.flags & SP_AIO_PARTIAL))
            return -ETIMEDOUT;
send_done:
        ahndl->out.flags = 0;
        *hndl = ahndl;
        *event = SP_AIO_OUT;
        *len = ahndl->out.olen - ahndl->out.len;
        return 0;

    case SP_POLLER_ERR:
error:
        /*  Socket error. */
        *hndl = ahndl;
        *event = SP_AIO_ERR;
        *len = 0;
        return 0;

    default:

        /*  Invalid event. */
        sp_assert (0);
        return 0;
    }
}

#endif

