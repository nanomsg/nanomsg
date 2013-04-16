/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#ifndef NN_TRANSPORT_INCLUDED
#define NN_TRANSPORT_INCLUDED

#include "nn.h"

#include "aio/aio.h"

#include "utils/list.h"
#include "utils/msg.h"

#include <stddef.h>
#include <stdint.h>

/*  This is the API between the nanomsg core and individual transports. */

struct nn_sock;
struct nn_cp;

/******************************************************************************/
/*  Container for transport-specific socket options.                          */
/******************************************************************************/

struct nn_optset;

struct nn_optset_vfptr {
    void (*destroy) (struct nn_optset *self);
    int (*setopt) (struct nn_optset *self, int option, const void *optval,
        size_t optvallen);
    int (*getopt) (struct nn_optset *self, int option, void *optval,
        size_t *optvallen);
};

struct nn_optset {
    const struct nn_optset_vfptr *vfptr;
};

/******************************************************************************/
/*  The base class for endpoints.                                             */
/******************************************************************************/

/*  The best way to think about endpoints is that endpoint is an object created
    by each nn_bind() or nn_connect() call. Each endpoint is associated with
    exactly one address string (e.g. "tcp://127.0.0.1:5555"). */

struct nn_epbase;

struct nn_epbase_vfptr {

    /*  Ask the endpoint to terminate itself. The endpoint is allowed to linger
        to send the pending outbound data. In such case the function returns
        -EINPROGRESS error. */
    int (*close) (struct nn_epbase *self);
};

/*  The members of this structure are used exclusively by the core. Never use
    or modify them directly from the transport. */
struct nn_epbase {
    const struct nn_epbase_vfptr *vfptr;
    struct nn_sock *sock;
    int eid;
    struct nn_list_item item;
    char addr [NN_SOCKADDR_MAX + 1];
};

/*  Creates a new endpoint. 'addr' parameter is the address string supplied
    by the user with the transport prefix chopped off. E.g. "127.0.0.1:5555"
    rather than "tcp://127.0.0.1:5555". 'hint' parameter is an opaque value that
    was passed to transport's bind or connect function. */
void nn_epbase_init (struct nn_epbase *self,
    const struct nn_epbase_vfptr *vfptr, const char *addr, void *hint);

/*  Destroys the endpoint, unregisters it from the socket etc. */
void nn_epbase_term (struct nn_epbase *self);

/*  Returns the default completion port associated with the current socket. */
struct nn_cp *nn_epbase_getcp (struct nn_epbase *self);

/*  Returns a worker. Each call to this function may return different worker. */
struct nn_worker *nn_epbase_choose_worker (struct nn_epbase *self);

/*  Returns the address string associated with this endpoint. */
const char *nn_epbase_getaddr (struct nn_epbase *self);

/*  Retrieve value of a socket option. */
void nn_epbase_getopt (struct nn_epbase *self, int level, int option,
    void *optval, size_t *optvallen);

/*  Returns 1 is the specified socket type is a valid peer for this socket,
    or 0 otherwise. */
int nn_epbase_ispeer (struct nn_epbase *self, int socktype);

/******************************************************************************/
/*  The base class for pipes.                                                 */
/******************************************************************************/

/*  Pipe represents one "connection", i.e. perfectly ordered uni- or
    bi-directional stream of messages. One endpoint can create multiple pipes
    (for example, bound TCP socket is an endpoint, individual accepted TCP
    connections are represented by pipes. */

struct nn_pipebase;

/*  This value is returned by pipe's send and recv functions to signalise that
    more sends/recvs are not possible at the moment. From that moment on,
    the core will stop invoking the function. To re-establish the message
    flow nn_pipebase_received (respectively nn_pipebase_sent) should
    be called. */
#define NN_PIPEBASE_RELEASE 1

/*  Specifies that received message is already split into header and body.
    This flag is used only by inproc transport to avoid merging and re-splitting
    the messages passed with a single process. */
#define NN_PIPEBASE_PARSED 2

struct nn_pipebase_vfptr {

    /*  Send a message to the network. The function can return either error
        (negative number) or any combination of the flags defined above. */
    int (*send) (struct nn_pipebase *self, struct nn_msg *msg);

    /*  Receive a message from the network. The function can return either error
        (negative number) or any combination of the flags defined above. */
    int (*recv) (struct nn_pipebase *self, struct nn_msg *msg);
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct nn_pipebase {
    const struct nn_pipebase_vfptr *vfptr;
    uint8_t instate;
    uint8_t outstate;
    struct nn_sock *sock;
    void *data;
};

/*  Initialise the pipe.  */
int nn_pipebase_init (struct nn_pipebase *self,
    const struct nn_pipebase_vfptr *vfptr, struct nn_epbase *epbase);

/*  Terminate the pipe. */
void nn_pipebase_term (struct nn_pipebase *self);

/*  Informs the user that the pipe is ready for sending and that asynchronous
    receiving of messages have already started. */
void nn_pipebase_activate (struct nn_pipebase *self);

/*  Call this function when new message was fully received. */
void nn_pipebase_received (struct nn_pipebase *self);

/*  Call this function when current outgoing message was fully sent. */
void nn_pipebase_sent (struct nn_pipebase *self);

/*  Returns the default completion port associated with the pipe. */
struct nn_cp *nn_pipebase_getcp (struct nn_pipebase *self);

/*  Returns 1 is the specified socket type is a valid peer for this socket,
    or 0 otherwise. */
int nn_pipebase_ispeer (struct nn_pipebase *self, int socktype);

/******************************************************************************/
/*  The transport class.                                                      */
/******************************************************************************/

struct nn_transport {

    /*  Name of the transport as it appears in the connection strings ("tcp",
        "ipc", "inproc" etc. */
    const char *name;

    /*  ID of the transport. */
    int id;

    /*  Following methods are guarded by a global critical section. Two of these
        function will never be invoked in parallel. The first is called when
        the library is initialised, the second one when it is terminated, i.e.
        when there are no more open sockets. */
    void (*init) (void);
    void (*term) (void);

    /*  Each of these functions creates an endpoint and returns the newly
        create endpoint in 'epbase' parameter. 'hint' is in opaque pointer
        to be passed to nn_epbase_init().  These functions are guarded by
        a socket-wide critical section. Two of these function will never be
        invoked in parallel on the same socket. */
    int (*bind) (const char *addr, void *hint, struct nn_epbase **epbase);
    int (*connect) (const char *addr, void *hint, struct nn_epbase **epbase);

    /*  Create an object to hold transport-specific socket options.
        Set this member to NULL in case there are no transport-specific
        socket options available. */
    struct nn_optset *(*optset) ();

    /*  This member is used exclusively by the core. Never touch it directly
        from the transport. */
    struct nn_list_item item;
};

#endif
