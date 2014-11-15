/*
    Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.

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

#include "ws.h"
#include "bws.h"
#include "cws.h"
#include "sws.h"

#include "../../websocket.h"

#include "../utils/port.h"
#include "../utils/iface.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"

#include <string.h>

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#endif

/*  WebSocket-specific socket options. */
struct nn_ws_optset {
    struct nn_optset base;
    int placeholder;
};

static void nn_ws_optset_destroy (struct nn_optset *self);
static int nn_ws_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen);
static int nn_ws_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen);
static const struct nn_optset_vfptr nn_ws_optset_vfptr = {
    nn_ws_optset_destroy,
    nn_ws_optset_setopt,
    nn_ws_optset_getopt
};

/*  nn_transport interface. */
static int nn_ws_bind (void *hint, struct nn_epbase **epbase);
static int nn_ws_connect (void *hint, struct nn_epbase **epbase);
static struct nn_optset *nn_ws_optset (void);

static struct nn_transport nn_ws_vfptr = {
    "ws",
    NN_WS,
    NULL,
    NULL,
    nn_ws_bind,
    nn_ws_connect,
    nn_ws_optset,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_ws = &nn_ws_vfptr;

static int nn_ws_bind (void *hint, struct nn_epbase **epbase)
{
    return nn_bws_create (hint, epbase);
}

static int nn_ws_connect (void *hint, struct nn_epbase **epbase)
{
    return nn_cws_create (hint, epbase); 
}

static struct nn_optset *nn_ws_optset ()
{
    struct nn_ws_optset *optset;

    optset = nn_alloc (sizeof (struct nn_ws_optset), "optset (ws)");
    alloc_assert (optset);
    optset->base.vfptr = &nn_ws_optset_vfptr;

    /*  Default values for WebSocket options. */
    optset->placeholder = 1000;

    return &optset->base;   
}

static void nn_ws_optset_destroy (struct nn_optset *self)
{
    struct nn_ws_optset *optset;

    optset = nn_cont (self, struct nn_ws_optset, base);
    nn_free (optset);
}

static int nn_ws_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen)
{
    struct nn_ws_optset *optset;

    optset = nn_cont (self, struct nn_ws_optset, base);

    switch (option) {
    case NN_WS_OPTION_PLACEHOLDER:
        if (optvallen != sizeof (int))
            return -EINVAL;
        optset->placeholder = *(int*) optval;
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

static int nn_ws_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen)
{
    struct nn_ws_optset *optset;

    optset = nn_cont (self, struct nn_ws_optset, base);

    switch (option) {
    case NN_WS_OPTION_PLACEHOLDER:
        memcpy (optval, &optset->placeholder,
            *optvallen < sizeof (int) ? *optvallen : sizeof (int));
        *optvallen = sizeof (int);
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

/*  Trims the WebSocket header from the message body. */
static int nn_ws_trim_header (struct nn_msghdr *msghdr, uint8_t *ws_hdr)
{
    /*  TODO: Rework to more appropriately use msg headers
        rather than prepending to the message body. */
    size_t len;
    void *chunk;

    if (msghdr->msg_iovlen <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (msghdr->msg_iov [0].iov_len <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (msghdr->msg_iov [0].iov_len == NN_MSG) {
        chunk = *(void**) msghdr->msg_iov [0].iov_base;
        len = nn_chunk_size (chunk);
        if (len >= sizeof (*ws_hdr)) {
            *ws_hdr = ((uint8_t *) chunk) [0];
            chunk = nn_chunk_trim (chunk, sizeof (*ws_hdr));
            *(void**) (msghdr->msg_iov[0].iov_base) = chunk;
            return sizeof (*ws_hdr);
        } 
        else {
            errno = EINVAL;
            return -1;
        }
    }
    else {
        /*  TODO: copy message into user-supplied buffer. */
        nn_assert (0);
    }
}

int nn_ws_send (int s, const void *buf, size_t len, uint8_t ws_hdr, int flags)
{
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    iov.iov_base = (void*) buf;
    iov.iov_len = len;

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = NULL;
    hdr.msg_controllen = 0;

    return nn_ws_sendmsg (s, &hdr, ws_hdr, flags);
}

int nn_ws_recv (int s, void *buf, size_t len, uint8_t *ws_hdr, int flags)
{
    struct nn_iovec iov;
    struct nn_msghdr hdr;

    iov.iov_base = buf;
    iov.iov_len = len;

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = NULL;
    hdr.msg_controllen = 0;

    return nn_ws_recvmsg (s, &hdr, ws_hdr, flags);
}

int nn_ws_sendmsg (int s, const struct nn_msghdr *msghdr, uint8_t ws_hdr,
    int flags)
{
    int rc;
    void *orig_buf;
    size_t orig_len;
    size_t added_len;
    uint8_t *new_chunk_buf;

    if (msghdr->msg_iovlen <= 0) {
        errno = EINVAL;
        return -1;
    }

    added_len = sizeof (ws_hdr);

    orig_buf = *((void**) msghdr->msg_iov[0].iov_base);
    orig_len = nn_chunk_size (orig_buf);
    rc = nn_chunk_alloc (orig_len + added_len, 0, (void**) &new_chunk_buf);
    if (rc != 0)
        return rc;

    /*  Assume this is not a continuation frame; if, in the future the library
        expands to support continuation frames, perhaps NN_SNDMORE is passed
        into "flags" which would not set the FIN bit. */
    new_chunk_buf [0] = ws_hdr | NN_SWS_FRAME_BITMASK_FIN;

    memcpy (&new_chunk_buf [1], orig_buf, orig_len);

    *(void**) (msghdr->msg_iov [0].iov_base) = new_chunk_buf;

    nn_chunk_free (orig_buf);

    rc = nn_sendmsg (s, msghdr, flags);

    if (rc < 0) {
        return rc;
    }
    else if (rc == 0) {
        /*  Impossible, since we just added data to the message. */
        nn_assert (0);
    }
    else {
        return (rc - added_len);
    }
}

int nn_ws_recvmsg (int s, struct nn_msghdr *msghdr, uint8_t *ws_hdr, int flags)
{
    int rc;
    int rc_trim;

    rc = nn_recvmsg (s, msghdr, flags);
    if (rc < 0)
        return rc;

    rc_trim = nn_ws_trim_header (msghdr, ws_hdr);

    /*  The library should always reassemble fragmented messages from
        remote endpoint. */
    nn_assert (*ws_hdr & NN_SWS_FRAME_BITMASK_FIN);

    /*  Return only the message type. */
    if (*ws_hdr == (NN_WS_MSG_TYPE_GONE | NN_SWS_FRAME_BITMASK_FIN))
        *ws_hdr = NN_WS_MSG_TYPE_GONE;
    else
        *ws_hdr &= NN_SWS_FRAME_BITMASK_OPCODE;

    if (rc_trim < 0)
        return rc_trim;
    else
        return rc - rc_trim;
}
