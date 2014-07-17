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
#include "../src/ipc.h"

#include "testutil.h"

/*  Tests IPC transport. */

#define SOCKET_ADDRESS "ipc://test.ipc"

#define BATCH_SEND 10

int main ()
{
	int pub;
	int sub;
	int rc;

	pub = test_socket (AF_SP, NN_PUB);
	test_bind (pub, SOCKET_ADDRESS);

	sub = test_socket (AF_SP, NN_SUB);
	rc = nn_setsockopt (sub, NN_SUB, NN_SUB_SUBSCRIBE, "", 0);
	errno_assert (rc == 0);
	test_connect (sub, SOCKET_ADDRESS);

	/* Wait for connection to establish. */
	nn_sleep (10);

	test_send (pub, "one");
	test_recv (sub, "one");

	test_close (pub);
	pub = test_socket (AF_SP, NN_PUB);
	test_bind (pub, SOCKET_ADDRESS);

	nn_sleep (10);

	test_send (pub, "two");
	test_recv (sub, "two"); /* Never completes. */

	test_close (sub);
	test_close (pub);

	return 0;
}
