/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.
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

#ifndef WS_H_INCLUDED
#define WS_H_INCLUDED

#include "nn.h"
#include "utils\int.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NN_WS -4

/*  Socket options  */
#define NN_WS_OPTION_PLACEHOLDER 1

/*  WebSocket opcode constants as per RFC 6455 5.2  */
#define NN_WS_MSG_TYPE_TEXT 0x01
#define NN_WS_MSG_TYPE_BINARY 0x02
#define NN_WS_MSG_TYPE_CLOSE 0x08
#define NN_WS_MSG_TYPE_PING 0x09
#define NN_WS_MSG_TYPE_PONG 0x0A

/*  This opcode is defined exclusively by nanomsg to indicate that
    the library is closing the connection based on invalid data received
    from the peer. */
#define NN_WS_MSG_TYPE_GONE 0x7F

/*  Convenience wrappers for send/recv that automatically
    append/trim the WebSocket header from the message body. Return values
    have same semantics as the send/recv methods they wrap. */
NN_EXPORT int nn_ws_send (int s, const void *buf, size_t len,
    uint8_t ws_msg_type, int flags);
NN_EXPORT int nn_ws_sendmsg (int s, const struct nn_msghdr *msghdr,
    uint8_t ws_msg_type, int flags);
NN_EXPORT int nn_ws_recv (int s, void *buf, size_t len,
    uint8_t *ws_msg_type, int flags);
NN_EXPORT int nn_ws_recvmsg (int s, struct nn_msghdr *msghdr,
    uint8_t *ws_msg_type, int flags);

/*  Convenience wrapper for synchronously pinging the remote peer. Return
    value is round-trip time in milliseconds. */
//  TODO: Ping/Pong is not currently exposed on the W3C editor's draft, so
//  perhaps we should not expose it either. See:
//  http://dev.w3.org/html5/websockets/#ping-and-pong-frames
//NN_EXPORT int nn_ws_ping (int s, void *body, int flags);

#ifdef __cplusplus
}
#endif

#endif

