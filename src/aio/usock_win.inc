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

#include "worker.h"

#include "../utils/err.h"
#include "../utils/cont.h"
#include "../utils/alloc.h"

#include <stddef.h>
#include <string.h>

#define NN_USOCK_STATE_IDLE 1
#define NN_USOCK_STATE_STARTING 2
#define NN_USOCK_STATE_BEING_ACCEPTED 3
#define NN_USOCK_STATE_ACCEPTED 4
#define NN_USOCK_STATE_CONNECTING 5
#define NN_USOCK_STATE_ACTIVE 6
#define NN_USOCK_STATE_CANCELLING_IO 7
#define NN_USOCK_STATE_DONE 8
#define NN_USOCK_STATE_LISTENING 9
#define NN_USOCK_STATE_ACCEPTING 10
#define NN_USOCK_STATE_CANCELLING 11
#define NN_USOCK_STATE_STOPPING 12
#define NN_USOCK_STATE_STOPPING_ACCEPT 13

#define NN_USOCK_ACTION_ACCEPT 1
#define NN_USOCK_ACTION_BEING_ACCEPTED 2
#define NN_USOCK_ACTION_CANCEL 3
#define NN_USOCK_ACTION_LISTEN 4
#define NN_USOCK_ACTION_CONNECT 5
#define NN_USOCK_ACTION_ACTIVATE 6
#define NN_USOCK_ACTION_DONE 7
#define NN_USOCK_ACTION_ERROR 8

/*  Private functions. */
static void nn_usock_handler (struct nn_fsm *self, int src, int type,
    void *srcptr);
static int nn_usock_cancel_io (struct nn_usock *self);

void nn_usock_init (struct nn_usock *self, int src, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_usock_handler, src, self, owner);
    self->state = NN_USOCK_STATE_IDLE;
    self->s = INVALID_SOCKET;
    nn_worker_op_init (&self->in, &self->fsm);
    nn_worker_op_init (&self->out, &self->fsm);
    self->domain = -1;
    self->type = -1;
    self->protocol = -1;

    /*  Intialise events raised by usock. */
    nn_fsm_event_init (&self->event_established);
    nn_fsm_event_init (&self->event_sent);
    nn_fsm_event_init (&self->event_received);
    nn_fsm_event_init (&self->event_error);

    /*  No accepting is going on at the moment. */
    self->asock = NULL;
    self->ainfo = NULL;
}

void nn_usock_term (struct nn_usock *self)
{
    nn_assert (self->state == NN_USOCK_STATE_IDLE);

    if (self->ainfo)
        nn_free (self->ainfo);
    nn_fsm_event_term (&self->event_error);
    nn_fsm_event_term (&self->event_received);
    nn_fsm_event_term (&self->event_sent);
    nn_fsm_event_term (&self->event_established);
    nn_worker_op_term (&self->out);
    nn_worker_op_term (&self->in);
    nn_fsm_term (&self->fsm);
}

int nn_usock_isidle (struct nn_usock *self)
{
    return nn_fsm_isidle (&self->fsm);
}

int nn_usock_start (struct nn_usock *self, int domain, int type, int protocol)
{
    int rc;
#if defined IPV6_V6ONLY
    DWORD only;
#endif
#if defined HANDLE_FLAG_INHERIT
    BOOL brc;
#endif
    struct nn_worker *worker;
    HANDLE cp;

    /*  Open the underlying socket. */
    self->s = socket (domain, type, protocol);
    if (self->s == INVALID_SOCKET)
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    /*  Disable inheriting the socket to the child processes. */
#if defined HANDLE_FLAG_INHERIT
    brc = SetHandleInformation ((HANDLE) self->s, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);
#endif

    /*  IPv4 mapping for IPv6 sockets is disabled by default. Switch it on. */
#if defined IPV6_V6ONLY
    if (domain == AF_INET6) {
        only = 0;
        rc = setsockopt (self->s, IPPROTO_IPV6, IPV6_V6ONLY,
            (const char*) &only, sizeof (only));
        wsa_assert (rc != SOCKET_ERROR);
    }
#endif

    /*  Associate the socket with a worker thread/completion port. */
    worker = nn_fsm_choose_worker (&self->fsm);
    cp = CreateIoCompletionPort ((HANDLE) self->s,
        nn_worker_getcp (worker), (ULONG_PTR) NULL, 0);
    nn_assert (cp);

    /*  Remember the type of the socket. */
    self->domain = domain;
    self->type = type;
    self->protocol = protocol;

    /*  Start the state machine. */
    nn_fsm_start (&self->fsm);

    return 0;
}

void nn_usock_stop (struct nn_usock *self)
{
    nn_fsm_stop (&self->fsm);
}

void nn_usock_swap_owner (struct nn_usock *self, struct nn_fsm_owner *owner)
{
    nn_fsm_swap_owner (&self->fsm, owner);
}

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen)
{
    int rc;

    /*  The socket can be modified only before it's active. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING ||
        self->state == NN_USOCK_STATE_ACCEPTED);

    rc = setsockopt (self->s, level, optname, (char*) optval, optlen);
    if (nn_slow (rc == SOCKET_ERROR))
        return -nn_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    ULONG opt;

    /*  You can set socket options only before the socket is connected. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  On Windows, the bound port can be hijacked
        if SO_EXCLUSIVEADDRUSE is not set. */
    opt = 1;
    rc = setsockopt (self->s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char*) &opt, sizeof (opt));
    wsa_assert (rc != SOCKET_ERROR);

    rc = bind (self->s, addr, addrlen);
    if (nn_slow (rc == SOCKET_ERROR))
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    return 0;
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    int rc;

    /*  You can start listening only before the socket is connected. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  Start listening for incoming connections. */
    rc = listen (self->s, backlog);
    if (nn_slow (rc == SOCKET_ERROR))
       return -nn_err_wsa_to_posix (WSAGetLastError ());

    /*  Notify the state machine. */
    nn_fsm_action (&self->fsm, NN_USOCK_ACTION_LISTEN);

    return 0;
}

void nn_usock_accept (struct nn_usock *self, struct nn_usock *listener)
{
    int rc;
    BOOL brc;
    DWORD nbytes;

    rc = nn_usock_start (self, listener->domain, listener->type,
        listener->protocol);
    /*  TODO: EMFILE can be returned here. */
    errnum_assert (rc == 0, -rc);
    nn_fsm_action (&listener->fsm, NN_USOCK_ACTION_ACCEPT);
    nn_fsm_action (&self->fsm, NN_USOCK_ACTION_BEING_ACCEPTED);

    /*  If the memory for accept information is not yet allocated, do so.  */
    if (!listener->ainfo) {
        listener->ainfo = nn_alloc (512, "accept info");
        alloc_assert (listener->ainfo);
    }

    /*  Wait for the incoming connection. */
    memset (&listener->in.olpd, 0, sizeof (listener->in.olpd));
    brc = AcceptEx (listener->s, self->s, listener->ainfo, 0, 256, 256, &nbytes,
        &listener->in.olpd);

    /*  Immediate success. */
    if (nn_fast (brc == TRUE)) {
        nn_fsm_action (&listener->fsm, NN_USOCK_ACTION_DONE);
        nn_fsm_action (&self->fsm, NN_USOCK_ACTION_DONE);
        return;
    }

    /*  We don't expect a synchronous failure at this point. */
    wsa_assert (nn_slow (WSAGetLastError () == WSA_IO_PENDING));

    /*  Pair the two sockets. */
    nn_assert (!self->asock);
    self->asock = listener;
    nn_assert (!listener->asock);
    listener->asock = self;

    /*  Asynchronous accept. */
    nn_worker_op_start (&listener->in, 0);
}

void nn_usock_activate (struct nn_usock *self)
{
    nn_fsm_action (&self->fsm, NN_USOCK_ACTION_ACTIVATE);
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;
    BOOL brc;
    const GUID fid = WSAID_CONNECTEX;
    LPFN_CONNECTEX pconnectex;
    DWORD nbytes;

    /*  Fail if the socket is already connected, closed or such. */
    nn_assert (self->state == NN_USOCK_STATE_STARTING);

    /*  Notify the state machine that we've started connecting. */
    nn_fsm_action (&self->fsm, NN_USOCK_ACTION_CONNECT);

    /*  Get the pointer to connect function. */
    rc = WSAIoctl (self->s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        (void*) &fid, sizeof (fid), (void*) &pconnectex, sizeof (pconnectex),
        &nbytes, NULL, NULL);
    wsa_assert (rc == 0);
    nn_assert (nbytes == sizeof (pconnectex));

    /*  Connect itself. */
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
    brc = pconnectex (self->s, (struct sockaddr*) addr, addrlen,
        NULL, 0, NULL, &self->out.olpd);
    
    /*  Immediate success. */
    if (nn_fast (brc == TRUE)) {
        nn_fsm_action (&self->fsm, NN_USOCK_ACTION_DONE);
        return;
    }

    /*  Immediate error. */
    if (nn_slow (WSAGetLastError () != WSA_IO_PENDING)) {
        nn_fsm_action (&self->fsm, NN_USOCK_ACTION_ERROR);
        return;
    }

    /*  Asynchronous connect. */
    nn_worker_op_start (&self->out, 0);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iovec *iov,
    int iovcnt)
{
    int rc;
    WSABUF wbuf [NN_USOCK_MAX_IOVCNT];
    int i;

    /*  Make sure that the socket is actually alive. */
    nn_assert (self->state == NN_USOCK_STATE_ACTIVE);

    /*  Create a WinAPI-style iovec. */
    nn_assert (iovcnt <= NN_USOCK_MAX_IOVCNT);
    for (i = 0; i != iovcnt; ++i) {
        wbuf [i].buf = (char FAR*) iov [i].iov_base;
        wbuf [i].len = (u_long) iov [i].iov_len;
    }

    /*  Start the send opertation. */
    memset (&self->out.olpd, 0, sizeof (self->out.olpd));
    rc = WSASend (self->s, wbuf, iovcnt, NULL, 0, &self->out.olpd, NULL);
    if (nn_fast (rc == 0)) {
        nn_worker_op_start (&self->out, 0);
        return;
    }
    rc = WSAGetLastError ();
    if (nn_fast (rc == WSA_IO_PENDING)) {
        nn_worker_op_start (&self->out, 0);
        return;
    }

    if (rc == WSAECONNABORTED || rc == WSAECONNRESET ||
          rc == WSAENETDOWN || rc == WSAENETRESET ||
          rc == WSAENOBUFS || rc == WSAEWOULDBLOCK) {
        nn_fsm_action (&self->fsm, NN_USOCK_ACTION_ERROR);
        return;
   }

   wsa_assert (0);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    int rc;
    WSABUF wbuf;
    DWORD wflags;

    /*  Make sure that the socket is actually alive. */
    nn_assert (self->state == NN_USOCK_STATE_ACTIVE);

    /*  Start the receive operation. */
    wbuf.len = (u_long) len;
    wbuf.buf = (char FAR*) buf;
    wflags = MSG_WAITALL;
    memset (&self->in.olpd, 0, sizeof (self->in.olpd));
    rc = WSARecv (self->s, &wbuf, 1, NULL, &wflags, &self->in.olpd, NULL);
    if (nn_fast (rc == 0)) {
        nn_worker_op_start (&self->in, 1);
        return;
    }
    rc = WSAGetLastError ();
    if (nn_fast (rc == WSA_IO_PENDING)) {
        nn_worker_op_start (&self->in, 1);
        return;
    }

    if (rc == WSAECONNABORTED || rc == WSAECONNRESET ||
          rc == WSAENETDOWN || rc == WSAENETRESET ||
          rc == WSAETIMEDOUT || rc == WSAEWOULDBLOCK) {
        nn_fsm_action (&self->fsm, NN_USOCK_ACTION_ERROR);
        return;
   }

    wsa_assert (0);
}

static void nn_usock_handler (struct nn_fsm *self, int src, int type,
    void *srcptr)
{
    int rc;
    struct nn_usock *usock;

    usock = nn_cont (self, struct nn_usock, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (srcptr == NULL && type == NN_FSM_STOP)) {

        /*  Socket in ACCEPTING state cannot be closed.
            Stop the socket being accepted first. */
        nn_assert (usock->state != NN_USOCK_STATE_ACCEPTING);

        /*  Synchronous stop. */
        if (usock->state == NN_USOCK_STATE_IDLE)
            goto finish3;
        if (usock->state == NN_USOCK_STATE_DONE)
            goto finish2;
        if (usock->state == NN_USOCK_STATE_STARTING ||
              usock->state == NN_USOCK_STATE_ACCEPTED ||
              usock->state == NN_USOCK_STATE_LISTENING)
            goto finish1;

        /*  When socket that's being accepted is asked to stop, we have to
            ask the listener socket to stop accepting first. */
        if (usock->state == NN_USOCK_STATE_BEING_ACCEPTED) {
            nn_fsm_action (&usock->asock->fsm, NN_USOCK_ACTION_CANCEL);
            usock->state = NN_USOCK_STATE_STOPPING_ACCEPT;
            return;
        }

        /*  If we were already in the process of cancelling overlapped
            operations, we don't have to do anything. Continue waiting
            till cancelling is finished. */
        if (usock->state == NN_USOCK_STATE_CANCELLING_IO) {
            usock->state = NN_USOCK_STATE_STOPPING;
            return;
        }

        /*  In all remaining states we'll simply cancel all overlapped
            operations. */
        if (nn_usock_cancel_io (usock) == 0)
            goto finish1;
        usock->state = NN_USOCK_STATE_STOPPING;
        return;
    } 
    if (nn_slow (usock->state == NN_USOCK_STATE_STOPPING_ACCEPT)) {
        nn_assert (srcptr == NULL && type == NN_USOCK_ACTION_DONE);
        goto finish1;
    }
    if (nn_slow (usock->state == NN_USOCK_STATE_STOPPING)) {
        if (!nn_worker_op_isidle (&usock->in) ||
              !nn_worker_op_isidle (&usock->out))
            return;
finish1:
        rc = closesocket (usock->s);
        wsa_assert (rc == 0);
finish2:
        usock->state = NN_USOCK_STATE_IDLE;
        nn_fsm_stopped (&usock->fsm, NN_USOCK_STOPPED);
finish3:
        return;
    }

    switch (usock->state) {

/*****************************************************************************/
/*  IDLE state.                                                              */
/*****************************************************************************/
    case NN_USOCK_STATE_IDLE:
        if (srcptr == NULL) {
            switch (type) {
            case NN_FSM_START:
                usock->state = NN_USOCK_STATE_STARTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  STARTING state.                                                          */
/*****************************************************************************/
    case NN_USOCK_STATE_STARTING:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_LISTEN:
                usock->state = NN_USOCK_STATE_LISTENING;
                return;
            case NN_USOCK_ACTION_CONNECT:
                usock->state = NN_USOCK_STATE_CONNECTING;
                return;
            case NN_USOCK_ACTION_BEING_ACCEPTED:
                usock->state = NN_USOCK_STATE_BEING_ACCEPTED;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  BEING_ACCEPTED state.                                                    */
/*****************************************************************************/
    case NN_USOCK_STATE_BEING_ACCEPTED:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_DONE:
                usock->state = NN_USOCK_STATE_ACCEPTED;
                nn_fsm_raise (&usock->fsm, &usock->event_established,
                    NN_USOCK_ACCEPTED);
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  ACCEPTED state.                                                          */
/*****************************************************************************/
    case NN_USOCK_STATE_ACCEPTED:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_ACTIVATE:
                usock->state = NN_USOCK_STATE_ACTIVE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  CONNECTING state.                                                        */
/*****************************************************************************/
    case NN_USOCK_STATE_CONNECTING:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_DONE:
                usock->state = NN_USOCK_STATE_ACTIVE;
                nn_fsm_raise (&usock->fsm, &usock->event_established,
                    NN_USOCK_CONNECTED);
                return;
            case NN_USOCK_ACTION_ERROR:
                rc = closesocket (usock->s);
                wsa_assert (rc == 0);
                usock->s = INVALID_SOCKET;
                usock->state = NN_USOCK_STATE_DONE;
                nn_fsm_raise (&usock->fsm, &usock->event_error, NN_USOCK_ERROR);
                return;
            default:
                nn_assert (0);
            }
        }
        if (srcptr == &usock->out) {
            switch (type) {
            case NN_WORKER_OP_DONE:
                usock->state = NN_USOCK_STATE_ACTIVE;
                nn_fsm_raise (&usock->fsm, &usock->event_established,
                    NN_USOCK_CONNECTED);
                return;
            case NN_WORKER_OP_ERROR:
                rc = closesocket (usock->s);
                wsa_assert (rc == 0);
                usock->s = INVALID_SOCKET;
                usock->state = NN_USOCK_STATE_DONE;
                nn_fsm_raise (&usock->fsm, &usock->event_error, NN_USOCK_ERROR);
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  ACTIVE state.                                                            */
/*****************************************************************************/
    case NN_USOCK_STATE_ACTIVE:
        if (srcptr == &usock->in) {
            switch (type) {
            case NN_WORKER_OP_DONE:
                nn_fsm_raise (&usock->fsm, &usock->event_received,
                    NN_USOCK_RECEIVED);
                return;
            case NN_WORKER_OP_ERROR:
                if (nn_usock_cancel_io (usock) == 0) {
                    usock->state = NN_USOCK_STATE_DONE;
                    return;
                }
                usock->state = NN_USOCK_STATE_CANCELLING_IO;
                return;
            default:
                nn_assert (0);
            }
        }
        if (srcptr == &usock->out) {
            switch (type) {
            case NN_WORKER_OP_DONE:
                nn_fsm_raise (&usock->fsm, &usock->event_sent, NN_USOCK_SENT);
                return;
            case NN_WORKER_OP_ERROR:
                if (nn_usock_cancel_io (usock) == 0) {
                    usock->state = NN_USOCK_STATE_DONE;
                    return;
                }
                usock->state = NN_USOCK_STATE_CANCELLING_IO;
                return;
            default:
                nn_assert (0);
            }
        }
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_ERROR:
                if (nn_usock_cancel_io (usock) == 0) {
                    usock->state = NN_USOCK_STATE_DONE;
                    return;
                }
                usock->state = NN_USOCK_STATE_CANCELLING_IO;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  CANCELLING_IO state.                                                     */
/*****************************************************************************/
    case NN_USOCK_STATE_CANCELLING_IO:
        if (srcptr == &usock->in || srcptr == &usock->out) {
            if (!nn_worker_op_isidle (&usock->in) ||
                  !nn_worker_op_isidle (&usock->out))
                return;
            usock->state = NN_USOCK_STATE_DONE;
            return;
        }
        nn_assert (0);

/*****************************************************************************/
/*  DONE state.                                                              */
/*****************************************************************************/
    case NN_USOCK_STATE_DONE:
        nn_assert (0);

/*****************************************************************************/
/*  LISTENING state.                                                         */
/*****************************************************************************/
    case NN_USOCK_STATE_LISTENING:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_ACCEPT:
                usock->state = NN_USOCK_STATE_ACCEPTING;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  ACCEPTING state.                                                         */
/*****************************************************************************/
    case NN_USOCK_STATE_ACCEPTING:
        if (srcptr == NULL) {
            switch (type) {
            case NN_USOCK_ACTION_DONE:
                usock->state = NN_USOCK_STATE_LISTENING;
                return;
            case NN_USOCK_ACTION_CANCEL:
                nn_usock_cancel_io (usock);
                usock->state = NN_USOCK_STATE_CANCELLING;
                return;
            default:
                nn_assert (0);
            }
        }
        if (srcptr == &usock->in) {
            switch (type) {
            case NN_WORKER_OP_DONE:

                /*  Adjust the new usock object. */
                usock->asock->state = NN_USOCK_STATE_ACCEPTED;

                /*  Notify the user that connection was accepted. */
                nn_fsm_raise (&usock->asock->fsm,
                    &usock->asock->event_established, NN_USOCK_ACCEPTED);

                /*  Disassociate the listener socket from the accepted
                    socket. */
                usock->asock->asock = NULL;
                usock->asock = NULL;

                /*  Wait till the user starts accepting once again. */
                usock->state = NN_USOCK_STATE_LISTENING;

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  CANCELLING state.                                                        */
/*****************************************************************************/
    case NN_USOCK_STATE_CANCELLING:
        if (srcptr == &usock->in) {
            switch (type) {
            case NN_WORKER_OP_DONE:
            case NN_WORKER_OP_ERROR:

                /*  TODO: The socket being accepted should be closed here. */

                usock->state = NN_USOCK_STATE_LISTENING;

                /*  Notify the accepted socket that it was stopped. */
                nn_fsm_action (&usock->asock->fsm, NN_USOCK_ACTION_DONE);

                return;

            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/*****************************************************************************/
/*  Invalid state.                                                           */
/*****************************************************************************/
    default:
        nn_assert (0);
    }
}

/*****************************************************************************/
/*  State machine actions.                                                   */
/*****************************************************************************/

/*  Returns 0 if there's nothing to cancel or 1 otherwise. */
static int nn_usock_cancel_io (struct nn_usock *self)
{
    int rc;
    BOOL brc;

    /*  For some reason simple CancelIo doesn't seem to work here.
        We have to use CancelIoEx instead. */
    rc = 0;
    if (!nn_worker_op_isidle (&self->in)) {
        brc = CancelIoEx ((HANDLE) self->s, &self->in.olpd);
        win_assert (brc || GetLastError () == ERROR_NOT_FOUND);
        rc = 1;
    }
    if (!nn_worker_op_isidle (&self->out)) {
        brc = CancelIoEx ((HANDLE) self->s, &self->out.olpd);
        win_assert (brc || ERROR_NOT_FOUND);
        rc = 1;
    }

    return rc;
}
