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

/* This will hang both on Windows and OSX */
#define SOCKET_ADDRESS "ipc://ipc_pub_disconnect.ipc"

/* This will hang on Windows, but work on OSX */
/* #define SOCKET_ADDRESS "tcp://0.0.0.0:8861" */

#define BATCH_SEND 10

int main ()
{
	int pub;
	int sub;
	int rc;
	char buf[10];

	pub = test_socket (AF_SP, NN_PUB);
	test_bind (pub, SOCKET_ADDRESS);

	sub = test_socket (AF_SP, NN_SUB);
	rc = nn_setsockopt (sub, NN_SUB, NN_SUB_SUBSCRIBE, "", 0);
	errno_assert (rc == 0);
	test_connect (sub, SOCKET_ADDRESS);

	/* Wait for connection to establish. */
	nn_sleep (100);

	test_send (pub, "one");
	/*
	  If TCP on Windows, this never completes.
	  I am seeing this with Windows IPC too if I have breakpoints in usock_win.c, so this looks like a race condition?
	*/
	test_recv (sub, "one");

	test_close (pub);

	pub = test_socket (AF_SP, NN_PUB);
	test_bind (pub, SOCKET_ADDRESS);

	/* Wait for the sub to reconnect. */
	nn_sleep (100);

	test_send (pub, "two");
#if 1
	/*
	  On both Windows and OSX, this never completes.
	  This should either reconnect, or return with an error indicating that the remote peer has closed.
	*/
	rc = nn_recv (sub, buf, sizeof(buf), 0);
	nn_assert (rc<0);
	rc = nn_errno();
	nn_assert (rc==ECONNRESET);
#else
	/* A more simple test, with NN_DONTWAIT this time. */
	rc = nn_recv (sub, buf, sizeof(buf), NN_DONTWAIT);
	nn_assert (rc<0);
	rc = nn_errno();
	/*
	  The pub was closed, the sub should either detect the problem or reconnect and read successfully rather than behaving as if no data is coming, forever.
	  This happens on both Windows and OSX.
	*/
	nn_assert (rc==ECONNRESET);
#endif

	test_close (sub);
	test_close (pub);

	return 0;
}
