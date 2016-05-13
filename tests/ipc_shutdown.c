/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.

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
#include "../src/utils/mutex.c"

/*  Stress test the IPC transport. */

#define THREAD_COUNT 100
#define TEST2_THREAD_COUNT 5
#define MESSAGES_PER_THREAD 100
#define TEST_LOOPS 10
#define SOCKET_ADDRESS "ipc://test-shutdown.ipc"

volatile int active;

static void routine (NN_UNUSED void *arg)
{
    int s;

    s = nn_socket (AF_SP, NN_SUB);
    if (s < 0 && nn_errno () == EMFILE)
        return;
    errno_assert (s >= 0);
    test_connect (s, SOCKET_ADDRESS);
    test_close (s);
}

static void routine2 (void *arg)
{
    int s;
    int i;

    struct nn_mutex *lock = arg;
    s = test_socket (AF_SP, NN_PULL);

    nn_mutex_lock(lock);
    nn_mutex_unlock(lock);

    for (i = 0; i < 10; ++i) {
        test_connect (s, SOCKET_ADDRESS);
    }

    for (i = 0; i < MESSAGES_PER_THREAD; ++i) {
        test_recv (s, "hello");
    }

    test_close (s);
    nn_mutex_lock(arg);
    active --;
    nn_mutex_unlock(arg);
}

int main ()
{
    int sb;
    int i;
    int j;
    int ms;
    struct nn_mutex lock;
    struct nn_thread threads [THREAD_COUNT];

    /*  Stress the shutdown algorithm. */

#if defined(SIGPIPE) && defined(SIG_IGN)
	signal (SIGPIPE, SIG_IGN);
#endif

    sb = test_socket (AF_SP, NN_PUB);
    test_bind (sb, SOCKET_ADDRESS);

    for (j = 0; j != TEST_LOOPS; ++j) {
        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_init (&threads [i], routine, NULL);
        for (i = 0; i != THREAD_COUNT; ++i)
            nn_thread_term (&threads [i]);
    }

    test_close (sb);

    /*  Test race condition of sending message while socket shutting down  */

    sb = test_socket (AF_SP, NN_PUSH);
    test_bind (sb, SOCKET_ADDRESS);
    nn_sleep(100);
    ms = 20;
    test_setsockopt (sb, NN_SOL_SOCKET, NN_SNDTIMEO, &ms, sizeof (ms));

    nn_mutex_init(&lock);
    for (j = 0; j != TEST_LOOPS; ++j) {

        nn_mutex_lock(&lock);
        active = TEST2_THREAD_COUNT;
        for (i = 0; i != TEST2_THREAD_COUNT; ++i)
            nn_thread_init (&threads [i], routine2, &lock);
        nn_mutex_unlock(&lock);

        for (;;) {
            nn_mutex_lock (&lock);
            if (active == 0) {
                nn_mutex_unlock (&lock);
                break;
            }
            nn_mutex_unlock (&lock);
            (void) nn_send (sb, "hello", 5, 0);
        }

        for (i = 0; i != TEST2_THREAD_COUNT; ++i)
            nn_thread_term (&threads [i]);
    }

    test_close (sb);

    return 0;
}

