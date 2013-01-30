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

#include "../compat/zmq/zmq.h"

#include "../src/utils/err.c"

#include <string.h>
#include <stdint.h>

int main ()
{
    int rc;
    void *ctx;
    void *s1;
    void *s2;
    uint64_t zmq_sndbuf;
    int nn_sndbuf;
    zmq_msg_t msg1;
    zmq_msg_t msg2;

    ctx = zmq_init (1);
    errno_assert (ctx);

    /*  Open a PAIR socket using ZMQ-specific name. */
    s1 = zmq_socket (ctx, ZMQ_PAIR);
    errno_assert (s1);

    /*  Open a PAIR socket using NN-specific constant. */
    s2 = zmq_socket (ctx, 1016);
    errno_assert (s2);

    /*  Check wiring up the topology. */
    rc = zmq_bind (s1, "inproc://a");
    errno_assert (rc == 0);
    rc = zmq_connect (s2, "inproc://a");
    errno_assert (rc == 0);

    /*  Set an option using ZMQ-specific name. */
    zmq_sndbuf = 128 * 1024;
    rc = zmq_setsockopt (s1, ZMQ_SNDBUF, &zmq_sndbuf, sizeof (zmq_sndbuf));
    errno_assert (rc == 0);

    /*  Set an option using NN-specific constant. */
    nn_sndbuf = 128 * 1024;
    rc = zmq_setsockopt (s2, 1002, &nn_sndbuf, sizeof (nn_sndbuf));
    errno_assert (rc == 0);

    /*  Send a message. */
    rc = zmq_msg_init_size (&msg1, 3);
    errno_assert (rc == 0);
    memcpy (zmq_msg_data (&msg1), "ABC", 3);
    rc = zmq_send (s2, &msg1, 0);
    errno_assert (rc == 0);
    rc = zmq_msg_close (&msg1);
    errno_assert (rc == 0);

    /*  Receive a message. */
    rc = zmq_msg_init (&msg2);
    errno_assert (rc == 0);
    rc = zmq_recv (s1, &msg2, 0);
    errno_assert (rc == 0);
    nn_assert (zmq_msg_size (&msg2) == 3);
    rc = zmq_msg_close (&msg2);
    errno_assert (rc == 0);

    /*  Clean up. */
    rc = zmq_close (s2);
    errno_assert (rc == 0);
    rc = zmq_close (s1);
    errno_assert (rc == 0);
    rc = zmq_term (ctx);
    errno_assert (rc == 0);

    return 0;
}

