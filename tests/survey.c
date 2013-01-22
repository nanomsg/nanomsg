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
#include "../src/survey.h"

#include "../src/utils/err.c"

int main ()
{
    int rc;
    int surveyor;
    int respondent1;
    int respondent2;
    int deadline;
    char buf [7];

    /*  Test a simple survey with two respondents. */

    rc = nn_init ();
    errno_assert (rc == 0);
    surveyor = nn_socket (AF_SP, NN_SURVEYOR);
    errno_assert (surveyor != -1);
    deadline = 100;
    rc = nn_setsockopt (surveyor, NN_SURVEYOR, NN_DEADLINE,
        &deadline, sizeof (deadline));
    errno_assert (rc == 0);
    rc = nn_bind (surveyor, "inproc://a");
    errno_assert (rc >= 0);
    respondent1 = nn_socket (AF_SP, NN_RESPONDENT);
    errno_assert (respondent1 != -1);
    rc = nn_connect (respondent1, "inproc://a");
    errno_assert (rc >= 0);
    respondent2 = nn_socket (AF_SP, NN_RESPONDENT);
    errno_assert (respondent2 != -1);
    rc = nn_connect (respondent2, "inproc://a");
    errno_assert (rc >= 0);

    rc = nn_send (surveyor, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_recv (respondent1, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (respondent1, "DEF", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_recv (respondent2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (respondent2, "DEF", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_recv (surveyor, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (surveyor, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    rc = nn_recv (surveyor, buf, sizeof (buf), 0);
    errno_assert (rc == -1 && nn_errno () == EFSM);

    rc = nn_close (surveyor);
    errno_assert (rc == 0);
    rc = nn_close (respondent1);
    errno_assert (rc == 0);    
    rc = nn_close (respondent2);
    errno_assert (rc == 0);
    rc = nn_term ();
    errno_assert (rc == 0);

    return 0;
}

