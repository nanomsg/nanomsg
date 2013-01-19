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

#ifndef SP_PROTOCOL_INCLUDED
#define SP_PROTOCOL_INCLUDED

#include "utils/aio.h"
#include "utils/cond.h"
#include "utils/list.h"
#include "utils/clock.h"
#include "utils/msg.h"

#include <stddef.h>
#include <stdint.h>

/******************************************************************************/
/*  Pipe class.                                                               */
/******************************************************************************/

/*  Any combination of following flags can be returned from successful call
    to sp_pipe_send or sp_pipe_recv. */
#define SP_PIPE_RELEASE 1
#define SP_PIPE_PARSED 2

struct sp_pipe;

void sp_pipe_setdata (struct sp_pipe *self, void *data);
void *sp_pipe_getdata (struct sp_pipe *self);
int sp_pipe_send (struct sp_pipe *self, struct sp_msg *msg);
int sp_pipe_recv (struct sp_pipe *self, struct sp_msg *msg);

/******************************************************************************/
/*  Base class for all socket types.                                          */
/******************************************************************************/

struct sp_sockbase;

/*  To be implemented by individual socket types. */
struct sp_sockbase_vfptr {
    void (*destroy) (struct sp_sockbase *self);
    int (*add) (struct sp_sockbase *self, struct sp_pipe *pipe);
    void (*rm) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*in) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*out) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*send) (struct sp_sockbase *self, struct sp_msg *msg);
    int (*recv) (struct sp_sockbase *self, struct sp_msg *msg);
    int (*setopt) (struct sp_sockbase *self, int level, int option,
        const void *optval, size_t optvallen);
    int (*getopt) (struct sp_sockbase *self, int level, int option,
        void *optval, size_t *optvallen);
    int (*sethdr) (struct sp_msg *msg, const void *hdr, size_t hdrlen);
    int (*gethdr) (struct sp_msg *msg, void *hdr, size_t *hdrlen);
};

#define SP_SOCK_FLAG_ZOMBIE 1

struct sp_sockbase
{
    /*  Table of virtual functions supplied by the socket type. */
    const struct sp_sockbase_vfptr *vfptr;

    /*  Combination so SP_SOCK_FLAG_* flags. */
    int flags;

    /*  Completion port to handle file descriptors, timers and locking. */
    struct sp_cp cp;

    /*  Condition variable to implement sleeping in blocking socket
        operations. */
    struct sp_cond cond;

    /*  Clock used to measure send & recv timeouts. */
    struct sp_clock clock;

    /*  File descriptor for this socket. */
    int fd;

    /*  List of all active endpoints. */
    struct sp_list eps;

    /*  Endpoint ID to assign to the next endpoint. */
    int eid;

    /*  SP_SOL_SOCKET level options. */
    int linger;
    int sndbuf;
    int rcvbuf;
    int sndtimeo;
    int rcvtimeo;
    int reconnect_ivl;
    int reconnect_ivl_max;
};

/*  Initialise the socket. */
void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);

/*  If recv is blocking at the moment, this function will unblock it. */
void sp_sockbase_unblock_recv (struct sp_sockbase *self);

/*  If send is blocking at the moment, this function will unblock it. */
void sp_sockbase_unblock_send (struct sp_sockbase *self);

/*  Returns completion port associated with the socket. */
struct sp_cp *sp_sockbase_getcp (struct sp_sockbase *self);

/******************************************************************************/
/*  The socktype class.                                                       */
/******************************************************************************/

/*  This structure defines a class factory for individual socket types. */

struct sp_socktype {
    int domain;
    int protocol;
    struct sp_sockbase *(*create) (int fd);

    /*  This member is owned by the core. Never touch it directly from inside
        the protocol implementation. */
    struct sp_list_item list;
};

#endif

