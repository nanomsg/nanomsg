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

#include "utils/aio.h"
#include "utils/cond.h"
#include "utils/list.h"
#include "utils/clock.h"
#include "utils/msg.h"
#include "utils/efd.h"

#include <stddef.h>
#include <stdint.h>

/******************************************************************************/
/*  Pipe class.                                                               */
/******************************************************************************/

/*  Any combination of following flags can be returned from successful call
    to nn_pipe_send or nn_pipe_recv. */
#define NN_PIPE_RELEASE 1
#define NN_PIPE_PARSED 2

struct nn_pipe;

void nn_pipe_setdata (struct nn_pipe *self, void *data);
void *nn_pipe_getdata (struct nn_pipe *self);
int nn_pipe_send (struct nn_pipe *self, struct nn_msg *msg);
int nn_pipe_recv (struct nn_pipe *self, struct nn_msg *msg);

/******************************************************************************/
/*  Base class for all socket types.                                          */
/******************************************************************************/

struct nn_sockbase;

/*  To be implemented by individual socket types. */
struct nn_sockbase_vfptr {
    void (*destroy) (struct nn_sockbase *self);
    int (*add) (struct nn_sockbase *self, struct nn_pipe *pipe);
    void (*rm) (struct nn_sockbase *self, struct nn_pipe *pipe);
    int (*in) (struct nn_sockbase *self, struct nn_pipe *pipe);
    int (*out) (struct nn_sockbase *self, struct nn_pipe *pipe);
    int (*send) (struct nn_sockbase *self, struct nn_msg *msg);
    int (*recv) (struct nn_sockbase *self, struct nn_msg *msg);
    int (*setopt) (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen);
    int (*getopt) (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen);
    int (*sethdr) (struct nn_msg *msg, const void *hdr, size_t hdrlen);
    int (*gethdr) (struct nn_msg *msg, void *hdr, size_t *hdrlen);
};

#define NN_SOCK_FLAG_ZOMBIE 1
#define NN_SOCK_FLAG_SNDFD 2
#define NN_SOCK_FLAG_RCVFD 4

struct nn_sockbase
{
    /*  Table of virtual functions supplied by the socket type. */
    const struct nn_sockbase_vfptr *vfptr;

    /*  Combination so NN_SOCK_FLAG_* flags. */
    int flags;

    /*  Completion port to handle file descriptors, timers and locking. */
    struct nn_cp cp;

    /*  Condition variable to implement sleeping in blocking socket
        operations. */
    struct nn_cond cond;

    /*  The efd objects for signalling readability and writeability. These
        objects are initialised only if NN_SOCK_FLAG_SNDFD/RCVFD flags are
        set. This way we can create these relatively expensive objcets only
        when actually needed. */
    struct nn_efd sndfd;
    struct nn_efd rcvfd;

    /*  Clock used to measure send & recv timeouts. */
    struct nn_clock clock;

    /*  File descriptor for this socket. */
    int fd;

    /*  List of all active endpoints. */
    struct nn_list eps;

    /*  Endpoint ID to assign to the next endpoint. */
    int eid;

    /*  NN_SOL_SOCKET level options. */
    int linger;
    int sndbuf;
    int rcvbuf;
    int sndtimeo;
    int rcvtimeo;
    int reconnect_ivl;
    int reconnect_ivl_max;
    int sndprio;
    int rcvprio;
};

/*  Initialise the socket. */
void nn_sockbase_init (struct nn_sockbase *self,
    const struct nn_sockbase_vfptr *vfptr, int fd);

/*  Uninitialise the socket. */
void nn_sockbase_term (struct nn_sockbase *self);

/*  If recv is blocking at the moment, this function will unblock it. */
void nn_sockbase_unblock_recv (struct nn_sockbase *self);

/*  If send is blocking at the moment, this function will unblock it. */
void nn_sockbase_unblock_send (struct nn_sockbase *self);

/*  Returns completion port associated with the socket. */
struct nn_cp *nn_sockbase_getcp (struct nn_sockbase *self);

/******************************************************************************/
/*  The socktype class.                                                       */
/******************************************************************************/

/*  This structure defines a class factory for individual socket types. */

struct nn_socktype {
    int domain;
    int protocol;
    struct nn_sockbase *(*create) (int fd);

    /*  This member is owned by the core. Never touch it directly from inside
        the protocol implementation. */
    struct nn_list_item list;
};

#endif

