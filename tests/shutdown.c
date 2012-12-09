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

#include "../src/utils/err.c"
#include "../src/utils/thread.c"
#include "../src/utils/sleep.c"

void worker (void *arg)
{
    int rc;
    int s;
    char buf [3];

    s = sp_socket (AF_SP, SP_PAIR);
    errno_assert (s != -1);
    rc = sp_recv (s, buf, sizeof (buf), 0);
    sp_assert (rc == -1 && sp_errno () == ETERM);
    rc = sp_close (s);
    errno_assert (rc == 0);
}

int main ()
{
    int rc;
    int s;
    struct sp_thread thread;

    /*  Close the socket with no associated endpoints. */
    rc = sp_init ();
    errno_assert (rc == 0);
    s = sp_socket (AF_SP, SP_PAIR);
    errno_assert (s != -1);
    rc = sp_close (s);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);

    /*  Test sp_term() before sp_close(). */
    rc = sp_init ();
    errno_assert (rc == 0);
    sp_thread_init (&thread, worker, NULL);
    sp_sleep (10);
    rc = sp_term ();
    errno_assert (rc == 0);
    sp_thread_term (&thread);

    return 0;
}

