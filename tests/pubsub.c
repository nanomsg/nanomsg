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
#include "../src/utils/sleep.c"

int main ()
{
    int rc;
    int pub;
    int sub1;
    int sub2;
    char buf [3];

    rc = sp_init ();
    errno_assert (rc == 0);
    pub = sp_socket (AF_SP, SP_PUB);
    errno_assert (pub != -1);
    rc = sp_bind (pub, "inproc://a");
    errno_assert (rc >= 0);
    sub1 = sp_socket (AF_SP, SP_SUB);
    errno_assert (sub1 != -1);
    rc = sp_setsockopt (sub1, SP_SOL_SOCKET, SP_SUBSCRIBE, "", 0);
    errno_assert (rc == 0);
    rc = sp_connect (sub1, "inproc://a");
    errno_assert (rc >= 0);
    sub2 = sp_socket (AF_SP, SP_SUB);
    errno_assert (sub2 != -1);
    rc = sp_setsockopt (sub2, SP_SOL_SOCKET, SP_SUBSCRIBE, "", 0);
    errno_assert (rc == 0);
    rc = sp_connect (sub2, "inproc://a");
    errno_assert (rc >= 0);

    /*  Wait till connections are established to prevent message loss. */
    sp_sleep (10);

    rc = sp_send (pub, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_recv (sub1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (sub2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_close (pub);
    errno_assert (rc == 0);
    rc = sp_close (sub1);
    errno_assert (rc == 0);    
    rc = sp_close (sub2);
    errno_assert (rc == 0);

    rc = sp_term ();
    errno_assert (rc == 0);

    return 0;
}

