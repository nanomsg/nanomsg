/*
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.
    Copyright (c) 2014 Martin Sustrik  All rights reserved.

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

#include "cws.h"
#include "sws.h"
#include "masker.h"
#include "sha1.h"

#include "../../ws.h"

#include "../utils/dns.h"
#include "../utils/port.h"
#include "../utils/iface.h"
#include "../utils/backoff.h"
#include "../utils/literal.h"
#include "../utils/base64.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/int.h"
#include "../../utils/attr.h"
#include "../../utils/random.h"
#include "../../utils/wire.h"

#include <string.h>

#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
#else
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#define NN_CWS_STATE_IDLE 1
#define NN_CWS_STATE_RESOLVING 2
#define NN_CWS_STATE_STOPPING_DNS 3
#define NN_CWS_STATE_CONNECTING 4
#define NN_CWS_STATE_SENDING_HDR 5
#define NN_CWS_STATE_RECEIVING_WSHDR 6
#define NN_CWS_STATE_RECEIVING_SPHDR 7
#define NN_CWS_STATE_ACTIVE 8
#define NN_CWS_STATE_STOPPING_SWS 9
#define NN_CWS_STATE_STOPPING_USOCK 10
#define NN_CWS_STATE_WAITING 11
#define NN_CWS_STATE_STOPPING_BACKOFF 12
#define NN_CWS_STATE_STOPPING_SWS_FINAL 13
#define NN_CWS_STATE_STOPPING 14

#define NN_CWS_SRC_USOCK 1
#define NN_CWS_SRC_RECONNECT_TIMER 2
#define NN_CWS_SRC_DNS 3
#define NN_CWS_SRC_SWS 4

#define NN_CWS_BUF_SIZE 2048

struct nn_cws {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  This object is a specific type of endpoint.
        Thus it is derived from epbase. */
    struct nn_epbase epbase;

    /*  The underlying WS socket. */
    struct nn_usock usock;

    /*  Used to wait before retrying to connect. */
    struct nn_backoff retry;

    /*  This buffer is used to store both outgoing WebSocket connection request
        and incoming reply. It's a pointer rather than static buffer so that
        it can be deallocated after the handshake. */
    uint8_t *buf;

    /*  When reading to buf, this number indicates how many bytes are already
        read. */
    size_t bufsz;

    /*  State machine that handles the active part of the connection
        lifetime. */
    struct nn_sws sws;

    /*  DNS resolver used to convert textual address into actual IP address
        along with the variable to hold the result. */
    struct nn_dns dns;
    struct nn_dns_result dns_result;
};

/*  nn_epbase virtual interface implementation. */
static void nn_cws_stop (struct nn_epbase *self);
static void nn_cws_destroy (struct nn_epbase *self);
const struct nn_epbase_vfptr nn_cws_epbase_vfptr = {
    nn_cws_stop,
    nn_cws_destroy
};

/*  Private functions. */
static void nn_cws_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_cws_shutdown (struct nn_fsm *self, int src, int type,
    void *srcptr);
static void nn_cws_start_resolving (struct nn_cws *self);
static void nn_cws_start_connecting (struct nn_cws *self,
    struct sockaddr_storage *ss, size_t sslen);
static void nn_cws_generate_key (char *buf, size_t len);

int nn_cws_create (void *hint, struct nn_epbase **epbase)
{
    int rc;
    const char *addr;
    size_t addrlen;
    const char *semicolon;
    const char *hostname;
    const char *colon;
    const char *end;
    struct sockaddr_storage ss;
    size_t sslen;
    int ipv4only;
    size_t ipv4onlylen;
    struct nn_cws *self;
    int reconnect_ivl;
    int reconnect_ivl_max;
    size_t sz;

    /*  Allocate the new endpoint object. */
    self = nn_alloc (sizeof (struct nn_cws), "cws");
    alloc_assert (self);

    /*  Initalise the endpoint. */
    nn_epbase_init (&self->epbase, &nn_cws_epbase_vfptr, hint);

    /*  Check whether IPv6 is to be used. */
    ipv4onlylen = sizeof (ipv4only);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_IPV4ONLY,
        &ipv4only, &ipv4onlylen);
    nn_assert (ipv4onlylen == sizeof (ipv4only));

    /*  Start parsing the address. */
    addr = nn_epbase_getaddr (&self->epbase);
    addrlen = strlen (addr);
    semicolon = strchr (addr, ';');
    hostname = semicolon ? semicolon + 1 : addr;
    colon = strrchr (addr, ':');
    end = addr + addrlen;

    /*  Parse the port. */
    if (nn_slow (!colon)) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }
    rc = nn_port_resolve (colon + 1, end - colon - 1);
    if (nn_slow (rc < 0)) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }

    /*  Check whether the host portion of the address is either a literal
        or a valid hostname. */
    if (nn_dns_check_hostname (hostname, colon - hostname) < 0 &&
          nn_literal_resolve (hostname, colon - hostname, ipv4only,
          &ss, &sslen) < 0) {
        nn_epbase_term (&self->epbase);
        return -EINVAL;
    }

    /*  If local address is specified, check whether it is valid. */
    if (semicolon) {
        rc = nn_iface_resolve (addr, semicolon - addr, ipv4only, &ss, &sslen);
        if (rc < 0) {
            nn_epbase_term (&self->epbase);
            return -ENODEV;
        }
    }

    /*  Initialise the structure. */
    nn_fsm_init_root (&self->fsm, nn_cws_handler, nn_cws_shutdown,
        nn_epbase_getctx (&self->epbase));
    self->state = NN_CWS_STATE_IDLE;
    nn_usock_init (&self->usock, NN_CWS_SRC_USOCK, &self->fsm);
    sz = sizeof (reconnect_ivl);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL,
        &reconnect_ivl, &sz);
    nn_assert (sz == sizeof (reconnect_ivl));
    sz = sizeof (reconnect_ivl_max);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RECONNECT_IVL_MAX,
        &reconnect_ivl_max, &sz);
    nn_assert (sz == sizeof (reconnect_ivl_max));
    if (reconnect_ivl_max == 0)
        reconnect_ivl_max = reconnect_ivl;
    nn_backoff_init (&self->retry, NN_CWS_SRC_RECONNECT_TIMER,
        reconnect_ivl, reconnect_ivl_max, &self->fsm);
    self->buf = NULL;
    nn_sws_init (&self->sws, NN_CWS_SRC_SWS, &self->epbase, &self->fsm);
    nn_dns_init (&self->dns, NN_CWS_SRC_DNS, &self->fsm);

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    /*  Return the base class as an out parameter. */
    *epbase = &self->epbase;

    return 0;
}

static void nn_cws_stop (struct nn_epbase *self)
{
    struct nn_cws *cws;

    cws = nn_cont (self, struct nn_cws, epbase);

    nn_fsm_stop (&cws->fsm);
}

static void nn_cws_destroy (struct nn_epbase *self)
{
    struct nn_cws *cws;

    cws = nn_cont (self, struct nn_cws, epbase);

    nn_dns_term (&cws->dns);
    nn_sws_term (&cws->sws);
    nn_backoff_term (&cws->retry);
    nn_usock_term (&cws->usock);
    nn_fsm_term (&cws->fsm);
    nn_epbase_term (&cws->epbase);
    if (cws->buf)
        nn_free (cws->buf);
    nn_free (cws);
}

static void nn_cws_shutdown (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_cws *cws;

    cws = nn_cont (self, struct nn_cws, fsm);

    if (nn_slow (src == NN_FSM_ACTION && type == NN_FSM_STOP)) {
        if (!nn_sws_isidle (&cws->sws)) {
            nn_epbase_stat_increment (&cws->epbase,
                NN_STAT_DROPPED_CONNECTIONS, 1);
            nn_sws_stop (&cws->sws);
        }
        cws->state = NN_CWS_STATE_STOPPING_SWS_FINAL;
    }
    if (nn_slow (cws->state == NN_CWS_STATE_STOPPING_SWS_FINAL)) {
        if (!nn_sws_isidle (&cws->sws))
            return;
        nn_backoff_stop (&cws->retry);
        nn_usock_stop (&cws->usock);
        nn_dns_stop (&cws->dns);
        cws->state = NN_CWS_STATE_STOPPING;
    }
    if (nn_slow (cws->state == NN_CWS_STATE_STOPPING)) {
        if (!nn_backoff_isidle (&cws->retry) ||
              !nn_usock_isidle (&cws->usock) ||
              !nn_dns_isidle (&cws->dns))
            return;
        cws->state = NN_CWS_STATE_IDLE;
        nn_fsm_stopped_noevent (&cws->fsm);
        nn_epbase_stopped (&cws->epbase);
        return;
    }

    nn_fsm_bad_state (cws->state, src, type);
}

static void nn_cws_handler (struct nn_fsm *self, int src, int type,
    NN_UNUSED void *srcptr)
{
    struct nn_cws *cws;
    struct nn_iovec iov;
    size_t sz;
    int protocol;
    struct nn_masker masker;
    char key [29];

    cws = nn_cont (self, struct nn_cws, fsm);

    switch (cws->state) {

/******************************************************************************/
/*  IDLE state.                                                               */
/*  The state machine wasn't yet started.                                     */
/******************************************************************************/
    case NN_CWS_STATE_IDLE:
        switch (src) {

        case NN_FSM_ACTION:
            switch (type) {
            case NN_FSM_START:
                nn_cws_start_resolving (cws);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  RESOLVING state.                                                          */
/*  Name of the host to connect to is being resolved to get an IP address.    */
/******************************************************************************/
    case NN_CWS_STATE_RESOLVING:
        switch (src) {

        case NN_CWS_SRC_DNS:
            switch (type) {
            case NN_DNS_DONE:
                nn_dns_stop (&cws->dns);
                cws->state = NN_CWS_STATE_STOPPING_DNS;
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_DNS state.                                                       */
/*  dns object was asked to stop but it haven't stopped yet.                  */
/******************************************************************************/
    case NN_CWS_STATE_STOPPING_DNS:
        switch (src) {

        case NN_CWS_SRC_DNS:
            switch (type) {
            case NN_DNS_STOPPED:
                if (cws->dns_result.error == 0) {
                    nn_cws_start_connecting (cws, &cws->dns_result.addr,
                        cws->dns_result.addrlen);
                    return;
                }
                nn_backoff_start (&cws->retry);
                cws->state = NN_CWS_STATE_WAITING;
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  CONNECTING state.                                                         */
/*  Non-blocking connect is under way.                                        */
/******************************************************************************/
    case NN_CWS_STATE_CONNECTING:
        switch (src) {

        case NN_CWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_CONNECTED:

                /*  Allocate the buffer to be used during the handshake. */
                nn_assert (cws->buf == NULL);
                cws->buf = nn_alloc (NN_CWS_BUF_SIZE, "cws handshake buffer");
                alloc_assert (cws->buf);

                /*  Create WebSocket connection request. */
                nn_cws_generate_key (key, sizeof (key));
                iov.iov_base = cws->buf;
                iov.iov_len = snprintf (cws->buf, NN_CWS_BUF_SIZE,
                        "GET / HTTP/1.1\r\n"
                        "Host: %s\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Key: %s\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "Sec-WebSocket-Protocol: sp\r\n\r\n",
                    "TODO", /* TODO: The host part of the connection string. */
                   key);
                nn_assert (iov.iov_len + 14 <= NN_CWS_BUF_SIZE);

                /*  Bundle SP header with the request. */
                sz = sizeof (protocol);
                nn_epbase_getopt (&cws->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
                    &protocol, &sz);
                nn_assert (sz == sizeof (protocol));
                cws->buf [iov.iov_len] = 0x82;
                cws->buf [iov.iov_len + 1] = 0x88;
                nn_random_generate (&cws->buf [iov.iov_len + 2], 4);
                cws->buf [iov.iov_len + 6] = 0x00;
                cws->buf [iov.iov_len + 7] = 'S';
                cws->buf [iov.iov_len + 8] = 'P';
                cws->buf [iov.iov_len + 9] = 0x00;
                nn_puts (&cws->buf [iov.iov_len + 10], protocol);
                cws->buf [iov.iov_len + 12] = 0x00;
                cws->buf [iov.iov_len + 13] = 0x00;
                nn_masker_init (&masker, &cws->buf [iov.iov_len + 2]);
                nn_masker_mask (&masker, &cws->buf [iov.iov_len + 6], 8);
                iov.iov_len += 14;

                /*  Send it to the peer. */
                nn_usock_send (&cws->usock, &iov, 1);
                cws->state = NN_CWS_STATE_SENDING_HDR;
                return;

            case NN_USOCK_ERROR:
                nn_epbase_set_error (&cws->epbase,
                    nn_usock_geterrno (&cws->usock));
                nn_usock_stop (&cws->usock);
                cws->state = NN_CWS_STATE_STOPPING_USOCK;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_INPROGRESS_CONNECTIONS, -1);
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_CONNECT_ERRORS, 1);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  SENDING_HDR state.                                                        */
/*  WebSocket request (with bundled SP header) is being sent.                 */
/******************************************************************************/
    case NN_CWS_STATE_SENDING_HDR:
        switch (src) {

        case NN_CWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SENT:

                /*  Start reading the reply. It is at least 15 bytes long. */
                nn_assert (NN_CWS_BUF_SIZE >= 15);
                nn_usock_recv (&cws->usock, cws->buf, 15, NULL);
                cws->bufsz = 15;
                cws->state = NN_CWS_STATE_RECEIVING_WSHDR;
                return;

            case NN_USOCK_SHUTDOWN:
            case NN_USOCK_ERROR:
                nn_epbase_set_error (&cws->epbase,
                    nn_usock_geterrno (&cws->usock));
                nn_usock_stop (&cws->usock);
                cws->state = NN_CWS_STATE_STOPPING_USOCK;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_INPROGRESS_CONNECTIONS, -1);
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_CONNECT_ERRORS, 1);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  RECEIVING_WSHDR state.                                                    */
/*  WebSocket reply is being received.                                        */
/******************************************************************************/
    case NN_CWS_STATE_RECEIVING_WSHDR:
        switch (src) {

        case NN_CWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_RECEIVED:

                /*  Check whether WebSocket connection request was fully read.
                    If not so, read one more byte and repeat. */
                nn_assert (cws->bufsz >= 4);
                if (memcmp (&cws->buf [cws->bufsz - 4], "\r\n\r\n", 4) != 0) {
                    nn_assert (NN_CWS_BUF_SIZE >= cws->bufsz + 1);
                    nn_usock_recv (&cws->usock,
                        &cws->buf [cws->bufsz], 1, NULL);
                    ++cws->bufsz;
                    return;
                }

                /*  TODO: Do reply validation here. */

                /*  When WebSocket response was received, read SP header. */
                nn_assert (NN_CWS_BUF_SIZE >= 10);
                nn_usock_recv (&cws->usock, cws->buf, 10, NULL);
                cws->bufsz = 10;
                cws->state = NN_CWS_STATE_RECEIVING_SPHDR;
                return;

            case NN_USOCK_SHUTDOWN:
            case NN_USOCK_ERROR:
                nn_epbase_set_error (&cws->epbase,
                    nn_usock_geterrno (&cws->usock));
                nn_usock_stop (&cws->usock);
                cws->state = NN_CWS_STATE_STOPPING_USOCK;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_INPROGRESS_CONNECTIONS, -1);
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_CONNECT_ERRORS, 1);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  RECEIVING_SPHDR state.                                                    */
/*  SP protocol header is being received.                                     */
/******************************************************************************/
    case NN_CWS_STATE_RECEIVING_SPHDR:
        switch (src) {

        case NN_CWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_RECEIVED:

                /* TODO: Do SP header validation here. */

                /*  Start normal communication. */
                nn_free (cws->buf);
                cws->buf = NULL;
                nn_sws_start (&cws->sws, &cws->usock, NN_SWS_MODE_CLIENT);
                cws->state = NN_CWS_STATE_ACTIVE;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_INPROGRESS_CONNECTIONS, -1);
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_ESTABLISHED_CONNECTIONS, 1);
                nn_epbase_clear_error (&cws->epbase);
                return;

            case NN_USOCK_SHUTDOWN:
            case NN_USOCK_ERROR:
                nn_epbase_set_error (&cws->epbase,
                    nn_usock_geterrno (&cws->usock));
                nn_usock_stop (&cws->usock);
                cws->state = NN_CWS_STATE_STOPPING_USOCK;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_INPROGRESS_CONNECTIONS, -1);
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_CONNECT_ERRORS, 1);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  ACTIVE state.                                                             */
/*  Connection is established and handled by the sws state machine.          */
/******************************************************************************/
    case NN_CWS_STATE_ACTIVE:
        switch (src) {

        case NN_CWS_SRC_SWS:
            switch (type) {
            case NN_SWS_ERROR:
                nn_sws_stop (&cws->sws);
                cws->state = NN_CWS_STATE_STOPPING_SWS;
                nn_epbase_stat_increment (&cws->epbase,
                    NN_STAT_BROKEN_CONNECTIONS, 1);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_SWS state.                                                      */
/*  sws object was asked to stop but it haven't stopped yet.                 */
/******************************************************************************/
    case NN_CWS_STATE_STOPPING_SWS:
        switch (src) {

        case NN_CWS_SRC_SWS:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_SWS_STOPPED:
                nn_usock_stop (&cws->usock);
                cws->state = NN_CWS_STATE_STOPPING_USOCK;
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_USOCK state.                                                     */
/*  usock object was asked to stop but it haven't stopped yet.                */
/******************************************************************************/
    case NN_CWS_STATE_STOPPING_USOCK:
        switch (src) {

        case NN_CWS_SRC_USOCK:
            switch (type) {
            case NN_USOCK_SHUTDOWN:
                return;
            case NN_USOCK_STOPPED:
                nn_backoff_start (&cws->retry);
                cws->state = NN_CWS_STATE_WAITING;
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  WAITING state.                                                            */
/*  Waiting before re-connection is attempted. This way we won't overload     */
/*  the system by continuous re-connection attemps.                           */
/******************************************************************************/
    case NN_CWS_STATE_WAITING:
        switch (src) {

        case NN_CWS_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_TIMEOUT:
                nn_backoff_stop (&cws->retry);
                cws->state = NN_CWS_STATE_STOPPING_BACKOFF;
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  STOPPING_BACKOFF state.                                                   */
/*  backoff object was asked to stop, but it haven't stopped yet.             */
/******************************************************************************/
    case NN_CWS_STATE_STOPPING_BACKOFF:
        switch (src) {

        case NN_CWS_SRC_RECONNECT_TIMER:
            switch (type) {
            case NN_BACKOFF_STOPPED:
                nn_cws_start_resolving (cws);
                return;
            default:
                nn_fsm_bad_action (cws->state, src, type);
            }

        default:
            nn_fsm_bad_source (cws->state, src, type);
        }

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_fsm_bad_state (cws->state, src, type);
    }
}

/******************************************************************************/
/*  State machine actions.                                                    */
/******************************************************************************/

static void nn_cws_start_resolving (struct nn_cws *self)
{
    const char *addr;
    const char *begin;
    const char *end;
    int ipv4only;
    size_t ipv4onlylen;

    /*  Extract the hostname part from address string. */
    addr = nn_epbase_getaddr (&self->epbase);
    begin = strchr (addr, ';');
    if (!begin)
        begin = addr;
    else
        ++begin;
    end = strrchr (addr, ':');
    nn_assert (end);

    /*  Check whether IPv6 is to be used. */
    ipv4onlylen = sizeof (ipv4only);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_IPV4ONLY,
        &ipv4only, &ipv4onlylen);
    nn_assert (ipv4onlylen == sizeof (ipv4only));

    /*  TODO: Get the actual value of IPV4ONLY option. */
    nn_dns_start (&self->dns, begin, end - begin, ipv4only, &self->dns_result);

    self->state = NN_CWS_STATE_RESOLVING;
}

static void nn_cws_start_connecting (struct nn_cws *self,
    struct sockaddr_storage *ss, size_t sslen)
{
    int rc;
    struct sockaddr_storage remote;
    size_t remotelen;
    struct sockaddr_storage local;
    size_t locallen;
    const char *addr;
    const char *end;
    const char *colon;
    const char *semicolon;
    uint16_t port;
    int ipv4only;
    size_t ipv4onlylen;
    int val;
    size_t sz;

    /*  Create IP address from the address string. */
    addr = nn_epbase_getaddr (&self->epbase);
    memset (&remote, 0, sizeof (remote));

    /*  Parse the port. */
    end = addr + strlen (addr);
    colon = strrchr (addr, ':');
    rc = nn_port_resolve (colon + 1, end - colon - 1);
    errnum_assert (rc > 0, -rc);
    port = rc;

    /*  Check whether IPv6 is to be used. */
    ipv4onlylen = sizeof (ipv4only);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_IPV4ONLY,
        &ipv4only, &ipv4onlylen);
    nn_assert (ipv4onlylen == sizeof (ipv4only));

    /*  Parse the local address, if any. */
    semicolon = strchr (addr, ';');
    memset (&local, 0, sizeof (local));
    if (semicolon)
        rc = nn_iface_resolve (addr, semicolon - addr, ipv4only,
            &local, &locallen);
    else
        rc = nn_iface_resolve ("*", 1, ipv4only, &local, &locallen);
    if (nn_slow (rc < 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_CWS_STATE_WAITING;
        return;
    }

    /*  Combine the remote address and the port. */
    remote = *ss;
    remotelen = sslen;
    if (remote.ss_family == AF_INET)
        ((struct sockaddr_in*) &remote)->sin_port = htons (port);
    else if (remote.ss_family == AF_INET6)
        ((struct sockaddr_in6*) &remote)->sin6_port = htons (port);
    else
        nn_assert (0);

    /*  Try to start the underlying socket. */
    rc = nn_usock_start (&self->usock, remote.ss_family, SOCK_STREAM, 0);
    if (nn_slow (rc < 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_CWS_STATE_WAITING;
        return;
    }

    /*  Set the relevant socket options. */
    sz = sizeof (val);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_SNDBUF, &val, &sz);
    nn_assert (sz == sizeof (val));
    nn_usock_setsockopt (&self->usock, SOL_SOCKET, SO_SNDBUF,
        &val, sizeof (val));
    sz = sizeof (val);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_RCVBUF, &val, &sz);
    nn_assert (sz == sizeof (val));
    nn_usock_setsockopt (&self->usock, SOL_SOCKET, SO_RCVBUF,
        &val, sizeof (val));

    /*  Bind the socket to the local network interface. */
    rc = nn_usock_bind (&self->usock, (struct sockaddr*) &local, locallen);
    if (nn_slow (rc != 0)) {
        nn_backoff_start (&self->retry);
        self->state = NN_CWS_STATE_WAITING;
        return;
    }

    /*  Start connecting. */
    nn_usock_connect (&self->usock, (struct sockaddr*) &remote, remotelen);
    self->state = NN_CWS_STATE_CONNECTING;
    nn_epbase_stat_increment (&self->epbase,
        NN_STAT_INPROGRESS_CONNECTIONS, 1);
}

static void nn_cws_generate_key (char *buf, size_t len)
{
    int rc;
    uint8_t rand_key [16];
    char encoded_key [25];
    struct nn_sha1 sha1;
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    /*  Check whether the key would fit into the buffer. */
    nn_assert (len >= 29);

    /*  Generate random 16-byte key as per RFC 6455 4.1 */
    nn_random_generate (rand_key, sizeof (rand_key));

    /*  Convert the key into Base64. */
    rc = nn_base64_encode (rand_key, sizeof (rand_key),
        encoded_key, sizeof (encoded_key));
    nn_assert (strlen (encoded_key) + 1 == sizeof (encoded_key));

    /*  Pre-calculated expected Accept Key value as per
        RFC 6455 section 4.2.2.5.4 (version December 2011). */
    nn_sha1_init (&sha1);
    nn_sha1_hash (&sha1, encoded_key, 24);
    nn_sha1_hash (&sha1, magic, 36);
    rc = nn_base64_encode (nn_sha1_result (&sha1),
        NN_SHA1_RESULT_LEN, buf, len);
    errnum_assert (rc >= 0, -rc);
    nn_assert (rc == 28);
}


