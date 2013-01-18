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
#include "../src/reqrep.h"

#include "../src/utils/err.c"

int main ()
{
    int rc;
    int rep;
    int req1;
    int req2;
    int resend_ivl;
    char buf [7];

    /*  Test req/rep with raw socket types. */

#if 0
    rc = sp_init ();
    errno_assert (rc == 0);
    rep = sp_socket (AF_SP_RAW, SP_REP);
    errno_assert (rep != -1);
    rc = sp_bind (rep, "inproc://a");
    errno_assert (rc >= 0);
    req1 = sp_socket (AF_SP_RAW, SP_REQ);
    errno_assert (req1 != -1);
    rc = sp_connect (req1, "inproc://a");
    errno_assert (rc >= 0);
    req2 = sp_socket (AF_SP_RAW, SP_REQ);
    errno_assert (req2 != -1);
    rc = sp_connect (req2, "inproc://a");
    errno_assert (rc >= 0);

    rc = sp_send (req2, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 7);
    rc = sp_send (rep, buf, 7, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 7);
    rc = sp_recv (req2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 7);
    rc = sp_send (rep, buf, 7, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 7);
    rc = sp_recv (req1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_close (rep);
    errno_assert (rc == 0);
    rc = sp_close (req1);
    errno_assert (rc == 0);    
    rc = sp_close (req2);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);
#endif

    /*  Test req/rep with full socket types. */

    rc = sp_init ();
    errno_assert (rc == 0);
    rep = sp_socket (AF_SP, SP_REP);
    errno_assert (rep != -1);
    rc = sp_bind (rep, "inproc://a");
    errno_assert (rc >= 0);
    req1 = sp_socket (AF_SP, SP_REQ);
    errno_assert (req1 != -1);
    rc = sp_connect (req1, "inproc://a");
    errno_assert (rc >= 0);
    req2 = sp_socket (AF_SP, SP_REQ);
    errno_assert (req2 != -1);
    rc = sp_connect (req2, "inproc://a");
    errno_assert (rc >= 0);

    rc = sp_send (rep, "ABC", 3, 0);
    sp_assert (rc == -1 && sp_errno () == EFSM);
    rc = sp_recv (req1, buf, sizeof (buf), 0);
    sp_assert (rc == -1 && sp_errno () == EFSM);

    rc = sp_send (req2, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_send (rep, buf, 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (req2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_send (rep, buf, 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (req1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_close (rep);
    errno_assert (rc == 0);
    rc = sp_close (req1);
    errno_assert (rc == 0);    
    rc = sp_close (req2);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);

    /*  Test re-sending of the request. */

    rc = sp_init ();
    errno_assert (rc == 0);
    rep = sp_socket (AF_SP, SP_REP);
    errno_assert (rep != -1);
    rc = sp_bind (rep, "inproc://a");
    errno_assert (rc >= 0);
    req1 = sp_socket (AF_SP, SP_REQ);
    errno_assert (req1 != -1);
    rc = sp_connect (req1, "inproc://a");
    errno_assert (rc >= 0);
    resend_ivl = 100;
    rc = sp_setsockopt (req1, SP_REQ, SP_RESEND_IVL,
        &resend_ivl, sizeof (resend_ivl));
    errno_assert (rc == 0);

    rc = sp_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);
    rc = sp_recv (rep, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    sp_assert (rc == 3);

    rc = sp_close (req1);
    errno_assert (rc == 0);
    rc = sp_close (rep);
    errno_assert (rc == 0);
    rc = sp_term ();
    errno_assert (rc == 0);

    return 0;
}

