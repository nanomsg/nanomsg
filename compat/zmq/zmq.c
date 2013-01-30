/*
    Copyright (c) 2013 250bpm s.r.o.

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

#include "zmq.h"
#include "zmq_utils.h"

#include "../../src/nn.h"
#include "../../src/pair.h"
#include "../../src/fanout.h"
#include "../../src/pubsub.h"
#include "../../src/reqrep.h"

#include "../../src/utils/err.h"
#include "../../src/utils/sleep.c"
#include "../../src/utils/stopwatch.c"
#include "../../src/utils/alloc.c"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*  Internally, zmq_msg_t structure is cast to this structure. */
struct nn_zmqmsg {
    void *data;
    size_t size;
};

/*  Make sure that nn_zmqmsg fits into zmq_msg_t. */
CT_ASSERT (sizeof (zmq_msg_t) >= sizeof (struct nn_zmqmsg));

/*  nanomsg has no context. Pointer to this variable will be used as
    a substitute context pointer. */
static const int nn_dummy_context = 0;

void zmq_version (int *major, int *minor, int *patch)
{
    *major = ZMQ_VERSION_MAJOR;
    *minor = ZMQ_VERSION_MINOR;
    *patch = ZMQ_VERSION_PATCH;
}

int zmq_errno ()
{
    return nn_errno ();
}

const char *zmq_strerror (int errnum)
{
    return nn_strerror (errnum);
}

int zmq_msg_init (zmq_msg_t *msg)
{
    struct nn_zmqmsg *zmqmsg;

    zmqmsg = (struct nn_zmqmsg*) msg;
    zmqmsg->data = NULL;
    zmqmsg->size = 0;
    return 0;
}

int zmq_msg_init_size (zmq_msg_t *msg, size_t size)
{
    struct nn_zmqmsg *zmqmsg;

    zmqmsg = (struct nn_zmqmsg*) msg;
    zmqmsg->size = size;
    zmqmsg->data = nn_allocmsg (size, 0);
    return zmqmsg->data ? 0 : -1;
}

int zmq_msg_init_data (zmq_msg_t *msg, void *data,
    size_t size, zmq_free_fn *ffn, void *hint)
{
    errno = ENOTSUP;
    return -1;
}

int zmq_msg_close (zmq_msg_t *msg)
{
    struct nn_zmqmsg *zmqmsg;

    zmqmsg = (struct nn_zmqmsg*) msg;
    if (!zmqmsg->data)
        return 0;
    return nn_freemsg (zmqmsg->data);
}

int zmq_msg_move (zmq_msg_t *dest, zmq_msg_t *src)
{
    struct nn_zmqmsg *zmqdest;
    struct nn_zmqmsg *zmqsrc;

    zmqdest = (struct nn_zmqmsg*) dest;
    zmqsrc = (struct nn_zmqmsg*) src;
    zmqdest->data = zmqsrc->data;
    zmqdest->size = zmqsrc->size;
    zmqsrc->data = NULL;
    zmqsrc->size = 0;
}

int zmq_msg_copy (zmq_msg_t *dest, zmq_msg_t *src)
{
    struct nn_zmqmsg *zmqdest;
    struct nn_zmqmsg *zmqsrc;

    /*  In nanomsg this functionality is not exposed to the user. Let's do
        an actual copy of the message instead. */
    zmqdest = (struct nn_zmqmsg*) dest;
    zmqsrc = (struct nn_zmqmsg*) src;
    zmqdest->size = zmqsrc->size;
    zmqdest->data = nn_allocmsg (zmqdest->size, 0);
    if (!zmqdest->data)
        return -1;
    memcpy (zmqdest->data, zmqsrc->data, zmqdest->size);
    return 0;
}

void *zmq_msg_data (zmq_msg_t *msg)
{
    struct nn_zmqmsg *zmqmsg;

    zmqmsg = (struct nn_zmqmsg*) msg;
    return zmqmsg->data;
}

size_t zmq_msg_size (zmq_msg_t *msg)
{
    struct nn_zmqmsg *zmqmsg;

    zmqmsg = (struct nn_zmqmsg*) msg;
    return zmqmsg->size;
}

void *zmq_init (int io_threads)
{
    int rc;

    /*  Initialise the nanomsg library. */
    rc = nn_init ();
    if (rc != 0)
        return NULL;

    /*  Return substitute context pointer. */
    return (void*) &nn_dummy_context;
}

int zmq_term (void *context)
{
    int rc;

    /*  Check whether substitute context pointer is valid. */
    if (context != &nn_dummy_context) {
        errno = EFAULT;
        return -1;
    }

    /*  Terminate the nanomsg library. */
    rc = nn_term ();
    if (rc != 0)
        return -1;

    return 0;
}

void *zmq_socket (void *context, int type)
{
    int domain;
    int protocol;
    int fd;

    /*  Check whether substitute context pointer is valid. */
    if (context != &nn_dummy_context) {
        errno = EFAULT;
        return NULL;
    }

    /*  Depending on the requested socket type, choose the most similar
        nanomsg socket type. */
    switch (type) {
    case ZMQ_PAIR:
        domain = AF_SP;
        protocol = NN_PAIR;
        break;
    case ZMQ_PUB:
        domain = AF_SP;
        protocol = NN_PUB;
        break;
    case ZMQ_SUB:
        domain = AF_SP;
        protocol = NN_SUB;
        break;
    case ZMQ_REQ:
        domain = AF_SP;
        protocol = NN_REQ;
        break;
    case ZMQ_REP:
        domain = AF_SP;
        protocol = NN_REP;
        break;
    case ZMQ_DEALER:
        domain = AF_SP_RAW;
        protocol = NN_REQ;
        break;
    case ZMQ_ROUTER:
        domain = AF_SP_RAW;
        protocol = NN_REP;
        break;
    case ZMQ_PULL:
        domain = AF_SP;
        protocol = NN_PULL;
        break;
    case ZMQ_PUSH:
        domain = AF_SP;
        protocol = NN_PUSH;
        break;
    case ZMQ_XPUB:
        domain = AF_SP_RAW;
        protocol = NN_PUB;
        break;
    case ZMQ_XSUB:
        domain = AF_SP_RAW;
        protocol = NN_SUB;
        break;
    default:

        /*  Mappings for native nanomsg socket types. */
        if (type < 1000 || type > 2999) {
            errno = EINVAL;
            return NULL;
        }
        else if (type < 2000) {
            domain = AF_SP;
            protocol = type - 1000;
        }
        else {
            domain = AF_SP_RAW;
            protocol = type - 2000;
        }
    }

    /*  Open the nanomsg socket. */
    fd = nn_socket (domain, protocol);
    if (fd < 0)
        return NULL;

    /*  Disguise file descriptor as a pointer. Also, add 1 to prevent NULL
        to be returned. */
    return (void*) (((uint8_t*) NULL) + fd + 1);
}

int zmq_close (void *s)
{
    int fd;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);
    return nn_close (fd);
}

int zmq_setsockopt (void *s, int option, const void *optval,
    size_t optvallen)
{
    int fd;
    int val;
    int level;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);

    /*  First, try to map ZeroMQ options to nanomsg options. */
    switch (option) {
    case ZMQ_SUBSCRIBE:
        return nn_setsockopt (fd, NN_SUB, NN_SUBSCRIBE, optval, optvallen);
    case ZMQ_UNSUBSCRIBE:
        return nn_setsockopt (fd, NN_SUB, NN_UNSUBSCRIBE, optval, optvallen);
    case ZMQ_SNDBUF:
        if (optvallen != sizeof (uint64_t)) {
            errno = EINVAL;
            return -1;
        }
        val = (int) (*(uint64_t*) optval);
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_SNDBUF, &val, sizeof (val));
    case ZMQ_RCVBUF:
        if (optvallen != sizeof (uint64_t)) {
            errno = EINVAL;
            return -1;
        }
        val = (int) (*(uint64_t*) optval);
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_RCVBUF, &val, sizeof (val));
    case ZMQ_LINGER:
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_LINGER, optval, optvallen);
    case ZMQ_RECONNECT_IVL:
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_RECONNECT_IVL,
            optval, optvallen);
    case ZMQ_RECONNECT_IVL_MAX:
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_RECONNECT_IVL_MAX,
            optval, optvallen);
    case ZMQ_RCVTIMEO:
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_RCVTIMEO,
            optval, optvallen);
    case ZMQ_SNDTIMEO:
        return nn_setsockopt (fd, NN_SOL_SOCKET, NN_SNDTIMEO,
            optval, optvallen);
    }

    /*  If the native ZeroMQ option is not supported by nanomsg, report it. */
    if (option >= 0 && option < 100) {
        errno = ENOTSUP;
        return -1;
    }

    /*  Provide a mechanism to expose native nanomsg options via ZeroMQ language
        bindings. */
    if (option < 0) {
        level = -(-option / 100);
        option = -option % 100;
    }
    else {
        level = option / 100;
        option %= 100;
        nn_assert (level > 0);
        --level;
    }
    return nn_setsockopt (fd, level, option, optval, optvallen);
}

int zmq_getsockopt (void *s, int option, void *optval,
    size_t *optvallen)
{
    errno = ENOTSUP;
    return -1;
}

int zmq_bind (void *s, const char *addr)
{
    int fd;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);
    rc = nn_bind (fd, addr);
    return rc >= 0 ? 0 : -1;
}

int zmq_connect (void *s, const char *addr)
{
    int fd;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);
    rc = nn_connect (fd, addr);
    return rc >= 0 ? 0 : -1;
}

int zmq_send (void *s, zmq_msg_t *msg, int flags)
{
    int fd;
    int nnflags;
    int rc;
    struct nn_zmqmsg *zmqmsg;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);

    if (flags & ZMQ_SNDMORE) {
        errno = EINVAL;
        return -1;
    }

    nnflags = 0;
    if (flags & ZMQ_NOBLOCK)
        nnflags |= NN_DONTWAIT;

    zmqmsg = (struct nn_zmqmsg*) msg;
    rc = nn_send (fd, &zmqmsg->data, NN_MSG, nnflags);
    if (rc < 0)
        return -1;
    zmqmsg->data = NULL;
    zmqmsg->size = 0;
    return 0;
}

int zmq_recv (void *s, zmq_msg_t *msg, int flags)
{
    int fd;
    int nnflags;
    int rc;
    struct nn_zmqmsg *zmqmsg;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) 0) - 1);

    nnflags = 0;
    if (flags & ZMQ_NOBLOCK)
        nnflags |= NN_DONTWAIT;

    zmqmsg = (struct nn_zmqmsg*) msg;
    rc = nn_recv (fd, &zmqmsg->data, NN_MSG, nnflags);
    if (rc < 0)
        return -1;
    zmqmsg->size = rc;
    return 0;
}

int zmq_poll (zmq_pollitem_t *items, int nitems, long timeout)
{
    errno = ENOTSUP;
    return -1;
}

int zmq_device (int device, void *frontend, void *backend)
{
    errno = ENOTSUP;
    return -1;
}

void *zmq_stopwatch_start ()
{
    struct nn_stopwatch *self;

    self = nn_alloc (sizeof (struct nn_stopwatch), "stopwatch");
    alloc_assert (self);
    nn_stopwatch_init (self);
    return (void*) self;
}

unsigned long zmq_stopwatch_stop (void *watch)
{
    return (unsigned long) nn_stopwatch_term ((struct nn_stopwatch*) watch);
}

void zmq_sleep (int seconds)
{
    nn_sleep (seconds * 1000);
}

