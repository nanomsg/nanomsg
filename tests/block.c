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

#include "../src/sp.h"
#include "../src/pair.h"

#include "../src/utils/err.c"
#include "../src/utils/thread.c"
#include "../src/utils/sleep.c"

/*  This test checks whether blocking on send/recv works as expected. */

int sc;
int sb;

void worker (void *arg)
{
    int rc;

    /*  Wait 0.1 sec for the main thread to block. */
    sp_sleep (100);

    rc = sp_send (sc, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    /*  Wait 0.1 sec for the main thread to process the previous message
        and block once again. */
    sp_sleep (100);

    rc = sp_send (sc, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
}

int main ()
{
    int rc;
    char buf [3];
    struct sp_thread thread;

    rc = sp_init ();
    errno_assert (rc == 0);
    sb = sp_socket (AF_SP, SP_PAIR);
    errno_assert (sb != -1);
    rc = sp_bind (sb, "inproc://a");
    errno_assert (rc >= 0);
    sc = sp_socket (AF_SP, SP_PAIR);
    errno_assert (sc != -1);
    rc = sp_connect (sc, "inproc://a");
    errno_assert (rc >= 0);

    sp_thread_init (&thread, worker, NULL);

    rc = sp_recv (sb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (sb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    sp_thread_term (&thread);

    rc = sp_close (sc);
    errno_assert (rc == 0);
    rc = sp_close (sb);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);

    return 0;
}

