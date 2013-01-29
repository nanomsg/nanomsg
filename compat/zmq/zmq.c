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

/*  Prevent name clashes between ZMQ and NN. */
#undef ETERM
#undef EFSM

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
    msg->data = NULL;
    msg->size = 0;
}

int zmq_msg_init_size (zmq_msg_t *msg, size_t size)
{
    msg->size = size;
    msg->data = nn_allocmsg (size, 0);
    return msg->data ? 0 : -1;
}

int zmq_msg_init_data (zmq_msg_t *msg, void *data,
    size_t size, zmq_free_fn *ffn, void *hint)
{
    errno = ENOTSUP;
    return -1;
}

int zmq_msg_close (zmq_msg_t *msg)
{
    return nn_freemsg (msg->data);
}

int zmq_msg_move (zmq_msg_t *dest, zmq_msg_t *src)
{
    dest->data = src->data;
    dest->size = src->size;
    src->data = NULL;
    src->size = 0;
}

int zmq_msg_copy (zmq_msg_t *dest, zmq_msg_t *src)
{
    /*  In nanomsg this functionality is not exposed to the user. Let's do
        an actual copy of the message instead. */
    dest->size = src->size;
    dest->data = nn_allocmsg (dest->size, 0);
    if (!dest->data)
        return -1;
    mempcy (dest->data, src->data, dest->size);
    return 0;
}

void *zmq_msg_data (zmq_msg_t *msg)
{
    return msg->data;
}

size_t zmq_msg_size (zmq_msg_t *msg)
{
    return msg->size;
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

    fd = (int) (((uint8_t*) s) - ((uint8_t*) - 1));
    return nn_close (fd);
}

int zmq_setsockopt (void *s, int option, const void *optval,
    size_t optvallen)
{
    nn_assert (0);
}

int zmq_getsockopt (void *s, int option, void *optval,
    size_t *optvallen)
{
    nn_assert (0);
}

int zmq_bind (void *s, const char *addr)
{
    int fd;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) - 1));
    rc = nn_bind (fd, addr);
    return rc >= 0 ? 0 : -1;
}

int zmq_connect (void *s, const char *addr)
{
    int fd;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) - 1));
    rc = nn_connect (fd, addr);
    return rc >= 0 ? 0 : -1;
}

int zmq_send (void *s, zmq_msg_t *msg, int flags)
{
    int fd;
    int nnflags;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) - 1));

    if (flags & ZMQ_SNDMORE) {
        errno = EINVAL;
        return -1;
    }

    nnflags = 0;
    if (flags & ZMQ_NOBLOCK)
        nnflags |= NN_DONTWAIT;

    rc = nn_send (fd, &msg->data, NN_MSG, nnflags);
    return rc < 0 ? -1 : 0;
}

int zmq_recv (void *s, zmq_msg_t *msg, int flags)
{
    int fd;
    int nnflags;
    int rc;

    fd = (int) (((uint8_t*) s) - ((uint8_t*) - 1));

    nnflags = 0;
    if (flags & ZMQ_NOBLOCK)
        nnflags |= NN_DONTWAIT;

    rc = nn_send (fd, &msg->data, NN_MSG, nnflags);
    return rc < 0 ? -1 : 0;
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

