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

#ifndef NN_CSTREAM_INCLUDED
#define NN_CSTREAM_INCLUDED

#include "stream.h"

#include "../../transport.h"

#include "../../aio/fsm.h"
#include "../../aio/usock.h"
#include "../../aio/timer.h"

/*  Returned by the resolve function to indicate that the 'local' address
    should be used. If the flag is not set, 'local' address should be
    ignored. */
#define NN_CSTREAM_DOBIND 1

struct nn_cstream;

/*  Virtual functions to be implemented by the specific stream type. */
struct nn_cstream_vfptr {
    int (*open) (struct nn_usock *usock, struct nn_fsm *owner);
    int (*resolve) (const char *addr,
        struct sockaddr_storage *local, socklen_t *locallen,
        struct sockaddr_storage *remote, socklen_t *remotelen);
};

struct nn_cstream {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  Virual functions to access specific transport type. */
    const struct nn_cstream_vfptr *vfptr;

    /*  This object is an endpoint. */
    struct nn_epbase epbase;

    /*  The underlying socket. */
    struct nn_usock usock;

    /*  There's at most one session per connecting endpoint, thus we can
        embed the session object directly into the connecter class. */
    struct nn_stream stream;

    /*  Current value of rety interval, in milliseconds. -1 means that
        value of NN_RECONNECT_IVL option should be used. */
    int retry_ivl;

    /*  Timer to wait before retrying to connect. */
    struct nn_timer retry_timer;
};

int nn_cstream_init (struct nn_cstream *self, const char *addr, void *hint,
    const struct nn_cstream_vfptr *vfptr);
void nn_cstream_term (struct nn_cstream *self);

#endif

