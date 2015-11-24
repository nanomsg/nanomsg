/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.

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

#include "../src/nn.h"
#include "../src/pair.h"
#include "../src/pubsub.h"
#include "../src/pipeline.h"
#include "../src/ipc.h"

#include "testutil.h"
#include "../src/utils/thread.c"

/*  Stress test the IPC transport. */

//#define THREAD_COUNT 100
#define THREAD_COUNT 10
#define MESSAGES_PER_THREAD 10
#define TEST_LOOPS 10
#define SOCKET_ADDRESS "ipc://test-xp.ipc"

volatile int active;

int nn_test_force_xp = 0;

static void routine (NN_UNUSED void *arg)
{
    int s;
    int i;

    s = test_socket (AF_SP, NN_PULL);

    for (i = 0; i < 10; ++i) {
        test_connect (s, SOCKET_ADDRESS);
    }

    for (i = 0; i < MESSAGES_PER_THREAD; ++i) {
        if (nn_test_force_xp) {
            /* Observed in XP mode:
               - nn_recv will return with 'Connection Reset'.
               - nn_recv will not return. */
            char buf[16];
            int rc;
            memset (buf, 0, sizeof(buf));
            rc = nn_recv (s, buf, sizeof(buf), 0);
            if (rc < 0) {
                fprintf (stderr, "nn_recv failed (expected Windows XP behavior) %s [%d]\n", nn_err_strerror (errno), (int) errno);
                break;
            } else {
                if (rc != 5) {
                    fprintf (stderr, "nn_recv wrong length\n");
                    nn_err_abort ();
                }
                if (memcmp ("hello", buf, 5) != 0) {
                    fprintf (stderr, "Received data is wrong\n");
                    nn_err_abort ();
                }
            }            
        } else {
            /* On Vista+ the send operation will always complete. */
            test_recv (s, "hello");
        }
    }

    test_close (s);
    active --;
}

int main ()
{
    int sb;
    int i;
    int j;
    struct nn_thread threads [THREAD_COUNT];

    /*  Test race condition of sending message while socket shutting down.
        This is stable in the Vista+ implementation, and will work unreliably in the XP implementation. */

    /*  Force XP mode since not everyone has an array of test VMs. */
    nn_test_force_xp = 1;
    nn_set_force_xp( 1 );

    sb = test_socket (AF_SP, NN_PUSH);
    test_bind (sb, SOCKET_ADDRESS);

    for (j = 0; j != TEST_LOOPS; ++j) {
        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_init (&threads [i], routine, NULL);
        active = THREAD_COUNT;

        while (active) {
            (void) nn_send (sb, "hello", 5, NN_DONTWAIT);
        }

        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_term (&threads [i]);
    }

    test_close (sb);

    return 0;
}

