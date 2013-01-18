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

#ifndef SP_TRANSPORT_INCLUDED
#define SP_TRANSPORT_INCLUDED

#include "sp.h"

#include "utils/list.h"
#include "utils/aio.h"
#include "utils/msg.h"

#include <stddef.h>
#include <stdint.h>

/*  This is the API between the SP core and individual transports. */

struct sp_sock;
struct sp_cp;

/******************************************************************************/
/*  The base class for endpoints.                                             */
/******************************************************************************/

struct sp_epbase;

struct sp_epbase_vfptr {

    /*  Ask the endpoint to terminate itself. The endpoint is allowed to linger
        to send the pending outbound data. Once it's ready, it will close itself
        using sp_epbase_term() which, in turn, will unregister the endpoint from
        the socket. */
    void (*close) (struct sp_epbase *self);
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct sp_epbase {
    const struct sp_epbase_vfptr *vfptr;
    struct sp_sock *sock;
    int eid;
    struct sp_list_item item;
    char addr [SP_SOCKADDR_MAX + 1];
};

/*  Creates a new endpoint. Returns the endpoint ID. */
void sp_epbase_init (struct sp_epbase *self,
    const struct sp_epbase_vfptr *vfptr, const char *addr, void *hint);

/*  Destroys the endpoint. */
void sp_epbase_term (struct sp_epbase *self);

/*  Returns the default completion port associated with the current socket. */
struct sp_cp *sp_epbase_getcp (struct sp_epbase *self);

/*  Returns the address associated with this endpoint. */
const char *sp_epbase_getaddr (struct sp_epbase *self);

/*  Retrieve value of a socket option. */
void sp_epbase_getopt (struct sp_epbase *self, int level, int option,
    void *optval, size_t *optvallen);

/******************************************************************************/
/*  The base class for pipes.                                                 */
/******************************************************************************/

struct sp_pipebase;

/*  This value is returned by pipe's send and recv functions to signalise that
    more sends/recvs are not possible at the moment. From that moment on,
    the core will stop invoking the function. To re-establish the message
    flow sp_pipebase_received (respectively sp_pipebase_sent) should
    be called. */
#define SP_PIPEBASE_RELEASE 1

/*  Internal pipe states. */
#define SP_PIPEBASE_INSTATE_DEACTIVATED 0
#define SP_PIPEBASE_INSTATE_IDLE 1
#define SP_PIPEBASE_INSTATE_RECEIVING 2
#define SP_PIPEBASE_INSTATE_RECEIVED 3
#define SP_PIPEBASE_INSTATE_ASYNC 4
#define SP_PIPEBASE_OUTSTATE_DEACTIVATED 0
#define SP_PIPEBASE_OUTSTATE_IDLE 1
#define SP_PIPEBASE_OUTSTATE_SENDING 2
#define SP_PIPEBASE_OUTSTATE_SENT 3
#define SP_PIPEBASE_OUTSTATE_ASYNC 4

struct sp_pipebase_vfptr {
    int (*send) (struct sp_pipebase *self, struct sp_msg *msg);
    int (*recv) (struct sp_pipebase *self, struct sp_msg *msg);
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct sp_pipebase {
    const struct sp_pipebase_vfptr *vfptr;
    uint8_t instate;
    uint8_t outstate;
    struct sp_epbase *epbase;
    void *data;
};

/*  Initialise the pipe.  */
int sp_pipebase_init (struct sp_pipebase *self,
    const struct sp_pipebase_vfptr *vfptr, struct sp_epbase *epbase);

/*  Terminate the pipe. */
void sp_pipebase_term (struct sp_pipebase *self);

/*  Informs the user that the pipe is ready for sending and that asynchronous
    receiving of messages have already started. */
void sp_pipebase_activate (struct sp_pipebase *self);

/*  Call this function when new message was fully received. */
void sp_pipebase_received (struct sp_pipebase *self);

/*  Call this function when current outgoing message was fully sent. */
void sp_pipebase_sent (struct sp_pipebase *self);

/*  Returns the default completion port associated with the pipe. */
struct sp_cp *sp_pipebase_getcp (struct sp_pipebase *self);


/******************************************************************************/
/*  The transport class.                                                      */
/******************************************************************************/

struct sp_transport {

    /*  Following methods are guarded by a global critical section. Two of these
        function will never be invoked in parallel. */
    const char *(*name) (void);
    void (*init) (void);
    void (*term) (void);

    /*  Following methods are guarded by a socket-wide critical section.
        Two of these function will never be invoked in parallel on the same
        socket. */
    int (*bind) (const char *addr, void *hint, struct sp_epbase **epbase);
    int (*connect) (const char *addr, void *hint, struct sp_epbase **epbase);

    /*  This member is owned by the core. Never touch it directly from
        the transport. */
    struct sp_list_item list;
};

#endif
