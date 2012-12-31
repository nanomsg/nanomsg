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
#include "../src/fanout.h"
#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

int main ()
{
    int rc;
    int push;
    int pull1;
    int pull2;
    char buf [3];

    rc = sp_init ();
    errno_assert (rc == 0);
    push = sp_socket (AF_SP, SP_PUSH);
    errno_assert (push != -1);
    rc = sp_bind (push, "inproc://a");
    errno_assert (rc >= 0);
    pull1 = sp_socket (AF_SP, SP_PULL);
    errno_assert (pull1 != -1);
    rc = sp_connect (pull1, "inproc://a");
    errno_assert (rc >= 0);
    pull2 = sp_socket (AF_SP, SP_PULL);
    errno_assert (pull2 != -1);
    rc = sp_connect (pull2, "inproc://a");
    errno_assert (rc >= 0);

    /*  Wait till both connections are established to get messages spread
        evenly between the two pull sockets. */
    sp_sleep (10);

    rc = sp_send (push, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_send (push, "DEF", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_recv (pull1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (pull2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_close (push);
    errno_assert (rc == 0);
    rc = sp_close (pull1);
    errno_assert (rc == 0);    
    rc = sp_close (pull2);
    errno_assert (rc == 0);

    rc = sp_term ();
    errno_assert (rc == 0);

    return 0;
}

