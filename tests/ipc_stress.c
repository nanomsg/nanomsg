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
#include "../src/utils/atomic.h"
#include "../src/utils/atomic.c"

/*  Stress test the IPC transport. */

#define THREAD_COUNT 10
#define TEST_LOOPS 10
#define SOCKET_ADDRESS "ipc://test-stress.ipc"

struct nn_atomic active;

static void server(NN_UNUSED void *arg)
{
	int bytes;
    int sock = nn_socket(AF_SP, NN_PULL);
    nn_assert(sock >= 0);
    nn_assert(nn_bind(sock, SOCKET_ADDRESS) >= 0);
    while (1)
    {
        char *buf = NULL;
        if (!active.n) break;
        bytes = nn_recv(sock, &buf, NN_MSG, 0);
        nn_assert(bytes >= 0);
        nn_freemsg(buf);
    }
    nn_close(sock);
}

static void client(NN_UNUSED void *arg)
{
	int bytes;
    char msg[] = "0";
    int sz_msg = strlen (msg) + 1; // '\0' too
    int i;

    for (i = 0; i < TEST_LOOPS; i++) {
        int cli_sock = nn_socket(AF_SP, NN_PUSH);
        nn_assert(cli_sock >= 0);
        nn_assert(nn_connect(cli_sock, SOCKET_ADDRESS) >= 0);
        bytes = nn_send(cli_sock, msg, sz_msg, 0);
        nn_assert(bytes == sz_msg);
        nn_close(cli_sock);
    }
    nn_atomic_dec(&active, 1);
}

int main()
{
#if 0
    int i;
	int cli_sock;
	int bytes;
    struct nn_thread srv_thread;
    struct nn_thread cli_threads[THREAD_COUNT];
    nn_atomic_init (&active, THREAD_COUNT);
    /*  Stress the shutdown algorithm. */
    nn_thread_init(&srv_thread, server, NULL);

    for (i = 0; i != THREAD_COUNT; ++i)
        nn_thread_init(&cli_threads[i], client, NULL);
    for (i = 0; i != THREAD_COUNT; ++i)
        nn_thread_term(&cli_threads[i]);

    active.n = 0;
    cli_sock = nn_socket(AF_SP, NN_PUSH);
    nn_assert(cli_sock >= 0);
    nn_assert(nn_connect(cli_sock, SOCKET_ADDRESS) >= 0);
    bytes = nn_send(cli_sock, &i, sizeof(i), 0);
    nn_assert(bytes == sizeof(i));
    nn_close(cli_sock);
    nn_thread_term(&srv_thread);
#endif 

    return 0;
}

