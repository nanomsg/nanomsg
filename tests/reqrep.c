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

#include "../src/nn.h"
#include "../src/reqrep.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"

#define SOCKET_ADDRESS "tcp://127.0.0.1:5555"

int main ()
{
    int rc;
    int rep1;
    int rep2;
    int req1;
    int req2;
    int resend_ivl;
    char buf [7];
    int timeo;

    /*  Test req/rep with full socket types. */
    rep1 = nn_socket (AF_SP, NN_REP);
    errno_assert (rep1 != -1);
    rc = nn_bind (rep1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    req1 = nn_socket (AF_SP, NN_REQ);
    errno_assert (req1 != -1);
    rc = nn_connect (req1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    req2 = nn_socket (AF_SP, NN_REQ);
    errno_assert (req2 != -1);
    rc = nn_connect (req2, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    /*  Check invalid sequence of sends and recvs. */
    rc = nn_send (rep1, "ABC", 3, 0);
    nn_assert (rc == -1 && nn_errno () == EFSM);
    rc = nn_recv (req1, buf, sizeof (buf), 0);
    nn_assert (rc == -1 && nn_errno () == EFSM);

    /*  Check fair queueing the requests. */
    rc = nn_send (req2, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (rep1, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (req2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (rep1, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (req1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_close (rep1);
    errno_assert (rc == 0);
    rc = nn_close (req1);
    errno_assert (rc == 0);    
    rc = nn_close (req2);
    errno_assert (rc == 0);

    /*  Check load-balancing of requests. */
    req1 = nn_socket (AF_SP, NN_REQ);
    errno_assert (req1 != -1);
    rc = nn_bind (req1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    rep1 = nn_socket (AF_SP, NN_REP);
    errno_assert (rep1 != -1);
    rc = nn_connect (rep1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    rep2 = nn_socket (AF_SP, NN_REP);
    errno_assert (rep2 != -1);
    rc = nn_connect (rep2, SOCKET_ADDRESS);
    errno_assert (rc >= 0);

    rc = nn_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (rep1, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (req1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (rep2, buf, 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (req1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_close (rep2);
    errno_assert (rc == 0);
    rc = nn_close (rep1);
    errno_assert (rc == 0);    
    rc = nn_close (req1);
    errno_assert (rc == 0);

    /*  Test re-sending of the request. */
    rep1 = nn_socket (AF_SP, NN_REP);
    errno_assert (rep1 != -1);
    rc = nn_bind (rep1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    req1 = nn_socket (AF_SP, NN_REQ);
    errno_assert (req1 != -1);
    rc = nn_connect (req1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    resend_ivl = 100;
    rc = nn_setsockopt (req1, NN_REQ, NN_REQ_RESEND_IVL,
        &resend_ivl, sizeof (resend_ivl));
    errno_assert (rc == 0);

    rc = nn_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_close (req1);
    errno_assert (rc == 0);
    rc = nn_close (rep1);
    errno_assert (rc == 0);

    /*  Check sending a request when the peer is not available. (It should
        be sent immediatelly when the peer comes online rather than relying
        on the resend algorithm. */
    req1 = nn_socket (AF_SP, NN_REQ);
    errno_assert (req1 != -1);
    rc = nn_connect (req1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    rc = nn_send (req1, "ABC", 3, 0);
    errno_assert (rc >= 0);

    nn_sleep (10);

    rep1 = nn_socket (AF_SP, NN_REP);
    errno_assert (rep1 != -1);
    rc = nn_bind (rep1, SOCKET_ADDRESS);
    errno_assert (rc >= 0);
    timeo = 100;
    rc = nn_setsockopt (rep1, NN_SOL_SOCKET, NN_RCVTIMEO,
       &timeo, sizeof (timeo));
    errno_assert (rc == 0);
    rc = nn_recv (rep1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_close (req1);
    errno_assert (rc == 0);
    rc = nn_close (rep1);
    errno_assert (rc == 0);

    return 0;
}

