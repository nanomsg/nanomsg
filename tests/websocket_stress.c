/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2014 Wirebird Labs LLC.  All rights reserved.

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
#include "../src/ws.h"

#include "../src/utils/int.h"

#include "testutil.h"

/*  Stress tests the WebSocket transport using Autobahn Testsuite. */

#define FUZZING_SERVER_ADDRESS "ws://127.0.0.1:9002"

/*  Control whether performances tests are run, which may add an additional
    minute or longer to the test. */
#define NN_WS_STRESS_SKIP_PERF 1

static void nn_ws_launch_fuzzing_client (NN_UNUSED void)
{
    FILE *fd;
    char *perf_tests;
    int rc;

    if (NN_WS_STRESS_SKIP_PERF)
        perf_tests = ", \"9.*\"";
    else
        perf_tests = "";

    /*  Create an Autobahn json config file in the same working directory
        as where the call to wstest will be. */
    fd = fopen ("fuzzingclient.json", "w+");
    errno_assert (fd != NULL);
    rc = fprintf (fd,
        "{\n"
        "    \"servers\": [\n"
        "                  {\n"
        "                    \"agent\": \"nanomsg\",\n"
        "                    \"url\" : \"%s\",\n"
        "                    \"protocols\" : [\"x-nanomsg-pair\"]\n"
        "                  }\n"
        "               ],\n"
        "    \"outdir\" : \"./reports/client\",\n"
        "    \"cases\" : [\"*\"],\n"
        "    \"exclude-cases\" : [\"6.4.3\", \"6.4.4\"%s],\n"
        "    \"exclude-agent-cases\" : {}\n"
        "}\n",
        FUZZING_SERVER_ADDRESS, perf_tests);

    errno_assert (rc > 0);
    rc = fclose (fd);
    errno_assert (rc == 0);
    /*  The following call launches a fuzzing client in an async
        process and assumes you have Autobahn Testsuite installed
        as per http://autobahn.ws/testsuite/installation.html */
#if defined NN_HAVE_WINDOWS
    rc = system (
        "start wstest "
        "--mode=fuzzingclient "
        "--spec=fuzzingclient.json");
#else
    rc = system (
        "wstest "
        "--mode=fuzzingclient "
        "--spec=fuzzingclient.json &");
#endif
    errno_assert (rc == 0);

}

static void nn_ws_launch_fuzzing_server (NN_UNUSED void)
{
    FILE *fd;
    char *perf_tests;
    int rc;

    if (NN_WS_STRESS_SKIP_PERF)
        perf_tests = ", \"9.*\"";
    else
        perf_tests = "";

    /*  Create an Autobahn json config file in the same working directory
        as where the call to wstest will be. */
    fd = fopen ("fuzzingserver.json", "w+");
    errno_assert (fd != NULL);
    rc = fprintf (fd,
        "{\n"
        "    \"url\": \"%s\",\n"
        "    \"protocols\" : [\"x-nanomsg-pair\"],\n"
        "    \"outdir\" : \"./reports/server\",\n"
        "    \"cases\" : [\"*\"],\n"
        "    \"exclude-cases\" : [\"6.4.3\", \"6.4.4\"%s],\n"
        "    \"exclude-agent-cases\" : {}\n"
        "}\n",
        FUZZING_SERVER_ADDRESS, perf_tests);

    errno_assert (rc > 0);
    rc = fclose (fd);
    errno_assert (rc == 0);

    /*  The following call launches a fuzzing server in an async
        process and assumes you have Autobahn Testsuite installed
        as per http://autobahn.ws/testsuite/installation.html */

#if defined NN_HAVE_WINDOWS
    rc = system (
        "start wstest "
        "--mode=fuzzingserver "
        "--spec=fuzzingserver.json "
        "--webport=0");
#else
    rc = system (
        "wstest "
        "--mode=fuzzingserver "
        "--spec=fuzzingserver.json "
        "--webport=0 &");
#endif
    errno_assert (rc == 0);

    /*  TODO: Failure to wait an arbitrarily-long-enough time for the
        test server to load results in STATUS_CONNECTION_REFUSED on the
        first query to interrogate number of cases. Why? Is this a
        nanomsg problem, Autobahn problem...? */
    nn_sleep (5000);
}

static void nn_ws_kill_autobahn (NN_UNUSED void)
{
    int rc;

#if defined NN_HAVE_WINDOWS
    rc = system ("taskkill /IM wstest.exe");
#else
    rc = system ("pkill Python");
#endif
    nn_assert (rc == 0);
}

static void nn_ws_autobahn_test (int socket, void *recv_buf, int test_id)
{
    int performing_test;
    int rc;
    uint8_t ws_msg_type;

    performing_test = 1;

    /*  Perform test until remote endpoint either initiates a Close
        Handshake, or if this endpoint fails the connection based on
        invalid input from the remote peer. */
    while (performing_test) {

        rc = nn_ws_recv (socket, recv_buf, NN_MSG, &ws_msg_type, 0);
        errno_assert (rc >= 0);

        printf ("Test %03d: Rx: 0x%02x (%d bytes)\n", test_id, ws_msg_type, rc);

        switch (ws_msg_type) {
        case NN_WS_MSG_TYPE_TEXT:
            /*  Echo text message verbatim. */
            rc = nn_ws_send (socket, recv_buf, NN_MSG, ws_msg_type, 0);
            break;
        case NN_WS_MSG_TYPE_BINARY:
            /*  Echo binary message verbatim. */
            rc = nn_ws_send (socket, recv_buf, NN_MSG, ws_msg_type, 0);
            break;
        case NN_WS_MSG_TYPE_PING:
            /*  As per RFC 6455 5.5.3, echo PING data payload as a PONG. */
            rc = nn_ws_send (socket, recv_buf, NN_MSG,
                NN_WS_MSG_TYPE_PONG, 0);
            break;
        case NN_WS_MSG_TYPE_PONG:
            /*  Silently ignore PONGs in this echo server. */
            break;
        case NN_WS_MSG_TYPE_CLOSE:
            /*  As per RFC 6455 5.5.1, repeat Close Code in message body. */
            rc = nn_ws_send (socket, recv_buf, NN_MSG, ws_msg_type, 0);
            performing_test = 0;
            break;
        case NN_WS_MSG_TYPE_GONE:
            /*  This indicates the remote peer has sent invalid data forcing
                the local endpoint to fail the connection as per RFC 6455. */
            performing_test = 0;
            printf ("Test %03d: correctly prevented remote endpoint fuzz\n",
                test_id);
            break;
        default:
            /*  The library delivered an unexpected message type. */
            nn_assert (0);
            break;
        }

        errno_assert (rc >= 0);
    }
}

int main ()
{
    int rc;
    int autobahn_server;
    int autobahn_client;
    int i;
    int cases;
    int local_connected_ep;
    int local_bound_ep;
    int recv_timeout;
    char autobahn_addr[64];
    uint8_t ws_msg_type;
    uint8_t *recv_buf = NULL;
    void *hdr_buf = NULL;

    autobahn_client = test_socket (AF_SP, NN_PAIR);

    /*  The longest intentional delay in a test as of Autobahn Testsuite v0.7.1
        is nominally 2sec, so a 5000msec timeout gives a bit of headroom. With
        performance tests enabled, some of those tests take 30sec or longer,
        depending on platform. */
    if (NN_WS_STRESS_SKIP_PERF)
        recv_timeout = 5000;
    else
        recv_timeout = 60000;

    nn_ws_launch_fuzzing_server ();

    nn_setsockopt (autobahn_client, NN_SOL_SOCKET, NN_RCVTIMEO, &recv_timeout,
        sizeof (recv_timeout));

    memset (autobahn_addr, 0, sizeof (autobahn_addr));
    sprintf (autobahn_addr, "%s/getCaseCount", FUZZING_SERVER_ADDRESS);

    printf ("Connecting to %s\n\n", autobahn_addr);
    local_connected_ep = test_connect (autobahn_client, autobahn_addr);
    errno_assert (local_connected_ep >= 0);

    printf ("Fetching cases...\n");
    rc = nn_ws_recv (autobahn_client, &recv_buf, NN_MSG, &ws_msg_type, 0);

    /*  We expect nominally three ASCII digits [0-9] representing total
        number of cases to run; as of Autobahn TestSuite v0.7.1,
        521+ test cases. 1 digit is allowed, to allow development of
        specific cases, and up to 4 digits, for potential future expansion
        of the test suite. */
    nn_assert (1 <= rc && rc <= 4);

    nn_assert (ws_msg_type == NN_WS_MSG_TYPE_TEXT);

    cases = 0;
    for (i = 0; i < rc; ++i) {
        nn_assert ('0' <= recv_buf [i] && recv_buf [i] <= '9');
        cases *= 10;
        cases += recv_buf [i] - '0';
    }

    rc = nn_freemsg (recv_buf);
    errno_assert (rc == 0);

    rc = nn_ws_recv (autobahn_client, &recv_buf, NN_MSG, &ws_msg_type, 0);

    /*  Autobahn Testsuite server sends a close handshake immediately
        following the number of test cases. */
    nn_assert (ws_msg_type == NN_WS_MSG_TYPE_CLOSE);

    /*  Currently the Autobahn Testsuite sends a zero-length payload on the
        close message, but we're allowing values greater than zero in case
        this changes in the future. */
    errno_assert (rc >= 0);

    /*  As per RFC 6455 5.5.1, repeat Close Code in message body. */
    rc = nn_ws_send (autobahn_client, &recv_buf, NN_MSG, ws_msg_type, 0);
    errno_assert (rc == 0);

    rc = nn_shutdown (autobahn_client, local_connected_ep);
    errno_assert (rc == 0);

    printf ("Preparing to run %d cases...\n", cases);

    /*  Notice, index starts with 1, not zero! */
    for (i = 1; i <= cases; i++) {
        sprintf (autobahn_addr, "%s/runCase?case=%d&agent=nanomsg",
            FUZZING_SERVER_ADDRESS, i);

        local_connected_ep = test_connect (autobahn_client, autobahn_addr);
        errno_assert (local_connected_ep >= 0);

        nn_ws_autobahn_test (autobahn_client, &recv_buf, i);

        rc = nn_shutdown (autobahn_client, local_connected_ep);
        errno_assert (rc == 0);
    }

    sprintf (autobahn_addr, "%s/updateReports?agent=nanomsg",
        FUZZING_SERVER_ADDRESS);

    printf ("Generating reports with %s ....\n", autobahn_addr);

    local_connected_ep = test_connect (autobahn_client, autobahn_addr);
    errno_assert (local_connected_ep >= 0);

    /*  Server sends close code as soon as the report is created. */
    rc = nn_ws_recv (autobahn_client, &recv_buf, NN_MSG, &ws_msg_type, 0);

    /*  Currently the Autobahn Testsuite sends a zero-length payload on the
        close message, but we're allowing values greater than zero in case
        this changes in the future. */
    errno_assert (rc >= 0);
    nn_assert (ws_msg_type == NN_WS_MSG_TYPE_CLOSE);

    /*  As per RFC 6455 5.5.1, repeat Close Code in message body. */
    rc = nn_ws_send (autobahn_client, &recv_buf, NN_MSG, ws_msg_type, 0);
    errno_assert (rc == 0);

    rc = nn_shutdown (autobahn_client, local_connected_ep);
    errno_assert (rc == 0);

    test_close (autobahn_client);
    
    nn_ws_kill_autobahn ();

    printf ("WebSocket client tests complete! Now, testing server...\n\n");
    
    /*  Create echo server for Autobahn Testsuite client fuzzer. */
    autobahn_server = test_socket (AF_SP, NN_PAIR);
    local_bound_ep = test_bind (autobahn_server, FUZZING_SERVER_ADDRESS);
    errno_assert (local_bound_ep >= 0);

    printf ("\n\nServer started on %s\n"
            "Waiting for Autobahn fuzzing client...\n\n",
            FUZZING_SERVER_ADDRESS);

    nn_ws_launch_fuzzing_client ();

    nn_setsockopt (autobahn_server, NN_SOL_SOCKET, NN_RCVTIMEO, &recv_timeout,
        sizeof (recv_timeout));

    /*  The same number of cases are tested for servers as for clients. */
    for (i = 1; i <= cases; i++) {
        nn_ws_autobahn_test (autobahn_server, &recv_buf, i);
    }

    /*  TODO: This hangs; why? It must be fixed. */
    //test_close (sb);

    return 0;
}
