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

#ifndef NN_PROTOCOL_INCLUDED
#define NN_PROTOCOL_INCLUDED

#include "aio/aio.h"

#include "utils/list.h"
#include "utils/clock.h"
#include "utils/msg.h"
#include "utils/efd.h"
#include "utils/sem.h"

#include <stddef.h>
#include <stdint.h>

/******************************************************************************/
/*  Pipe class.                                                               */
/******************************************************************************/

/*  Any combination of following flags can be returned from successful call
    to nn_pipe_send or nn_pipe_recv. */

/*  This flag means that the pipe can't be used for receiving (when returned
    from nn_pipe_recv()) or sending (when returned from nn_pipe_send()).
    Protocol implementation should not send/recv messages from the pipe until
    the pipe is revived by in()/out() function. */
#define NN_PIPE_RELEASE 1

/*  Specifies that received message is already split into header and body.
    This flag is used only by inproc transport to avoid merging and re-splitting
    the messages passed with a single process. */
#define NN_PIPE_PARSED 2

struct nn_pipe;

/*  Associates opaque pointer to protocol-specific data with the pipe. */
void nn_pipe_setdata (struct nn_pipe *self, void *data);

/*  Retrieves the opaque pointer associated with the pipe. */
void *nn_pipe_getdata (struct nn_pipe *self);

/*  Send the message to the pipe. If successful, pipe takes ownership of the
    messages. */
int nn_pipe_send (struct nn_pipe *self, struct nn_msg *msg);

/*  Receive a message from a pipe. 'msg' should not be initialised prior to
    the call. It will be initialised when the call succeeds. */
int nn_pipe_recv (struct nn_pipe *self, struct nn_msg *msg);

/******************************************************************************/
/*  Base class for all socket types.                                          */
/******************************************************************************/

struct nn_sockbase;

/*  The maximum implemented transport ID. */
#define NN_MAX_TRANSPORT 3

#define NN_SOCKBASE_EVENT_IN 1
#define NN_SOCKBASE_EVENT_OUT 2

/*  Specifies that the socket type can be never used to receive messages. */
#define NN_SOCKBASE_FLAG_NORECV 1

/*  Specifies that the socket type can be never used to send messages. */
#define NN_SOCKBASE_FLAG_NOSEND 2

/*  To be implemented by individual socket types. */
struct nn_sockbase_vfptr {

    /*  Returns any combination of the flags defined above. */
    int flags;

    /*  Returns 1 if the supplied socket type is a valid peer for this socket,
        0 otherwise. Note that the validation is done only within a single
        SP protocol. Peers speaking ather SP protocols are discarded by the
        core and socket is not even asked to validate them. */
    int (*ispeer) (int socktype);

    /*  Deallocate the socket. */
    void (*destroy) (struct nn_sockbase *self);

    /*  Management of pipes. 'add' registers a new pipe. The pipe cannot to
        send to or received from at the moment. 'rm' unregisters the pipe.
        The pipe should not be used after this call as it may already be
        deallocated. 'in' informs the socket that pipe is readable. 'out'
        informs it that it is writeable. */
    int (*add) (struct nn_sockbase *self, struct nn_pipe *pipe);
    void (*rm) (struct nn_sockbase *self, struct nn_pipe *pipe);
    void (*in) (struct nn_sockbase *self, struct nn_pipe *pipe);
    void (*out) (struct nn_sockbase *self, struct nn_pipe *pipe);

    /*  Return any combination of event flags defined above, thus specifying
        whether the socket should be readable, writeable or none. */
    int (*events) (struct nn_sockbase *self);

    /*  Send a message to the socket. Returns -EAGAIN if it cannot be done at
        the moment or zero in case of success. */
    int (*send) (struct nn_sockbase *self, struct nn_msg *msg);

    /*  Receive a message from the socket. Returns -EAGAIN if it cannot be done
        at the moment or zero in case of success. */
    int (*recv) (struct nn_sockbase *self, struct nn_msg *msg);

    /*  Set a protocol specific option. */
    int (*setopt) (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen);

    /*  Retrieve a protocol specific option. */
    int (*getopt) (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen);
};

/*  The members of this structure are used exclusively by the core. Never use
    or modify them directly from the protocol implementation. */
struct nn_sockbase
{
    const struct nn_sockbase_vfptr *vfptr;
    int flags;
    struct nn_cp cp;
    struct nn_efd sndfd;
    struct nn_efd rcvfd;
    struct nn_sem termsem;
    struct nn_clock clock;
    struct nn_list eps;
    int eid;
    int domain;
    int protocol;
    int linger;
    int sndbuf;
    int rcvbuf;
    int sndtimeo;
    int rcvtimeo;
    int reconnect_ivl;
    int reconnect_ivl_max;
    int sndprio;
    int rcvprio;
    struct nn_optset *optsets [NN_MAX_TRANSPORT];
};

/*  Initialise the socket. */
int nn_sockbase_init (struct nn_sockbase *self,
    const struct nn_sockbase_vfptr *vfptr);

/*  Uninitialise the socket. */
void nn_sockbase_term (struct nn_sockbase *self);

/*  Call this function when the state of the socket changed asynchronously,
    i.e. if the socket became (un)readable/(un)writeable outside of one of the
    nn_sockbase_vfptr functions, for example, in a timer handler or in a worker
    thread created by the protocol implementation. Calling it allows nanomsg
    core to check the new state and unblock any threads blocked in
    send/recv/poll functions as needed. */
void nn_sockbase_changed (struct nn_sockbase *self);

/*  Returns the completion port associated with the socket. */
struct nn_cp *nn_sockbase_getcp (struct nn_sockbase *self);

/******************************************************************************/
/*  The socktype class.                                                       */
/******************************************************************************/

/*  This structure defines a class factory for individual socket types. */

struct nn_socktype {

    /*  Domain and protocol IDs as specified in nn_socket() function. */
    int domain;
    int protocol;

    /*  Function to create the socket type. 'sockbase' is the output parameter
        to return reference to newly created socket. This function is called
        under global lock, so it is not possible that two sockets are being
        created in parallel. */
    int (*create) (struct nn_sockbase **sockbase);

    /*  This member is owned by the core. Never touch it directly from inside
        the protocol implementation. */
    struct nn_list_item item;
};

#endif

