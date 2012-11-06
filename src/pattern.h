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

#ifndef SP_PATTERN_INCLUDED
#define SP_PATTERN_INCLUDED

#include "utils/mutex.h"
#include "utils/thread.h"
#include "utils/clock.h"
#include "utils/cond.h"
#include "utils/list.h"
#include "utils/aio.h"

#include <stddef.h>
#include <stdint.h>

/******************************************************************************/
/*  Pipe class.                                                               */
/******************************************************************************/

#define SP_PIPE_RELEASE 1

struct sp_pipe;

void sp_pipe_setdata (struct sp_pipe *self, void *data);
void *sp_pipe_getdata (struct sp_pipe *self);
int sp_pipe_send (struct sp_pipe *self, const void *buf, size_t len);
int sp_pipe_recv (struct sp_pipe *self, void *buf, size_t *len);

/******************************************************************************/
/*  Timers.                                                                   */
/******************************************************************************/

struct sp_sockbase;

struct sp_timer {
    struct sp_list_item list;
    uint64_t timeout;
    void (*fn) (struct sp_timer *self);
};

/*  Start the timer. */
void sp_timer_start (struct sp_timer *self, struct sp_sockbase *sockbase,
    int timeout, void (*fn) (struct sp_timer *self));

/*  Cancel a running timer. */
void sp_timer_cancel (struct sp_timer *self, struct sp_sockbase *sockbase);

/******************************************************************************/
/*  Base class for all socket types.                                          */
/******************************************************************************/

struct sp_sockbase;

/*  To be implemented by individual socket types. */
struct sp_sockbase_vfptr {
    void (*term) (struct sp_sockbase *self);
    int (*add) (struct sp_sockbase *self, struct sp_pipe *pipe);
    void (*rm) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*in) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*out) (struct sp_sockbase *self, struct sp_pipe *pipe);
    int (*send) (struct sp_sockbase *self, const void *buf, size_t len);
    int (*recv) (struct sp_sockbase *self, void *buf, size_t *len);
    int (*setopt) (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen);
    int (*getopt) (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen);
};

struct sp_sockbase
{
    /*  Table of virtual functions supplied by the socket type. */
    const struct sp_sockbase_vfptr *vfptr;

    /*  Synchronises inbound-related state of the socket. */
    struct sp_mutex sync;

    /*  Condition variable to implement sleeping in blocking socket
        operations. */
    struct sp_cond cond;

    /*  File descriptor for this socket. */
    int fd;

    /*  Worker thread's instance of the clock. */
    struct sp_clock clock;

    /*  List of active timers. */
    struct sp_list timers;

    /*  Completion port processed the worker thread. */
    struct sp_cp cp;

    /*  Worker thread associated with the socket. */
    /*  At the moment there's one worker thread per socket. Later on we can
        move to thread pool model if needed. */
    struct sp_thread worker;
};

/*  Initialise the socket. */
void sp_sockbase_init (struct sp_sockbase *self,
    const struct sp_sockbase_vfptr *vfptr, int fd);

/*  Get access to the completion point associated with the socket. */
struct sp_cp *sp_sockbase_getcp (struct sp_sockbase *self);

/******************************************************************************/
/*  The socktype class.                                                       */
/******************************************************************************/

/*  This structure defines a class factory for individual socket types. */

struct sp_socktype {
    int domain;
    int protocol;
    struct sp_sockbase *(*create) (int fd);

    /*  This member is owned by the core. Never touch it directly from
        the pattern. */
    struct sp_list_item list;
};

#endif

