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

#ifndef SP_TRANSPORT_INCLUDED
#define SP_TRANSPORT_INCLUDED

#include "sp.h"

#include "utils/list.h"
#include "utils/aio.h"

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
        on sending unsent data for 'linger' milliseconds. The function returns
        zero if endpoint was successfully terminated or -EINPROGRESS in case
        the termination is delayed. In the latter case ... TODO */
    int (*close) (struct sp_epbase *self, int linger);
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct sp_epbase {
    const struct sp_epbase_vfptr *vfptr;
    struct sp_sock *sock;
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

/******************************************************************************/
/*  The base class for pipes.                                                 */
/******************************************************************************/

struct sp_pipebase;

/*  This value is returned by pipe's send and recv functions to signalise that
    more sends/recvs are not possible at the moment. From that moment on,
    the core will stop invoking the function. To re-establish the message
    flow sp_pipebase_in (respectively sp_pipebase_out) should be called. */
#define SP_PIPEBASE_RELEASE 1

/*  Functions in sp_pipe interface are guarded by socket-wide critical section.
    Same function on the same pipe in never called from different threads in
    parallel. */
struct sp_pipebase_vfptr {
    int (*send) (struct sp_pipebase *self, const void *buf, size_t len);
    int (*recv) (struct sp_pipebase *self, void *buf, size_t *len);
};

/*  The member of this structure are used internally by the core. Never use
    or modify them directly from the transport. */
struct sp_pipebase {
    const struct sp_pipebase_vfptr *vfptr;
    struct sp_epbase *epbase;
    struct sp_event inevent;
    struct sp_event outevent;
    void *data;
};

/*  Initialise the pipe. Once this function returns the pipe is transferred
    to a different critical section and should not be accessed from the original
    context. */
int sp_pipebase_init (struct sp_pipebase *self,
    const struct sp_pipebase_vfptr *vfptr, struct sp_epbase *epbase);

/*  Terminate the pipe. */
void sp_pipebase_term (struct sp_pipebase *self);

/*  Mark the pipe as ready for receiving. */
void sp_pipebase_in (struct sp_pipebase *self);

/*  Mark the pipe as ready for sending. */
void sp_pipebase_out (struct sp_pipebase *self);

/******************************************************************************/
/*  The transport class.                                                      */
/******************************************************************************/

/*  The methods in sp_transport interface are guarded by a global critical
    section. Two of these function will never be invoked in parallel. */
struct sp_transport {
    const char *(*name) (void);
    void (*init) (void);
    void (*term) (void);
    int (*bind) (const char *addr, void *hint, struct sp_epbase **epbase);
    int (*connect) (const char *addr, void *hint, struct sp_epbase **epbase);

    /*  This member is owned by the core. Never touch it directly from
        the transport. */
    struct sp_list_item list;
};

#endif
