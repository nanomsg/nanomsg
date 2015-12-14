/*
    Copyright (c) 2012 Martin Sustrik.  All rights reserved.
    Copyright (c) 2014-2015 Wirebird Labs LLC. All rights reserved.

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
#include "../src/utils/thread.c"

#include "testutil.h"

#include <string.h>

/*****************************************************************************/
/*  Stress tests the WebSocket transport using Autobahn Testsuite.           */
/*  To run this test, Autobahn Testsuite must be installed via:              */
/*  http://autobahn.ws/testsuite/installation.html                           */
/*****************************************************************************/

/*  Control whether performances tests are run, which may add an additional
    minute or longer to the test. */
#ifndef NN_WS_ENABLE_AUTOBAHN_PERF
    #define NN_WS_ENABLE_AUTOBAHN_PERF 0
#endif

/*  Used for test developer troubleshooting. */
#define NN_WS_AUTOBAHN_DEBUG 0

#define FUZZING_CLIENT_ADDRESS "ws://127.0.0.1:9002"
#define FUZZING_SERVER_ADDRESS "ws://127.0.0.1:9003"

/*  The longest intentional delay in a test as of Autobahn Testsuite v0.7.2
    is nominally 2sec, so a 5000msec timeout gives a bit of headroom. With
    performance tests enabled, some of those tests take 30sec or longer,
    depending on platform. */
#if NN_WS_ENABLE_AUTOBAHN_PERF
    #define NN_WS_EXCLUDE_CASES "[\"12.*\", \"13.*\"]"
    #define NN_WS_TEST_CASE_TIMEO 60000
    #define NN_WS_TEST_CASE_RCVMAX 16777216
#else
    #define NN_WS_EXCLUDE_CASES "[\"9.*\", \"12.*\", \"13.*\"]"
    #define NN_WS_TEST_CASE_TIMEO 5000
    #define NN_WS_TEST_CASE_RCVMAX 65536
#endif

#if NN_WS_AUTOBAHN_DEBUG
    #define NN_WS_DEBUG_AUTOBAHN_FLAG " --debug"
#else
    #define NN_WS_DEBUG_AUTOBAHN_FLAG ""
#endif

static int nn_ws_send (int s, const void *msg, size_t len, uint8_t msg_type, int flags)
{
    int rc;
    struct nn_iovec iov;
    struct nn_msghdr hdr;
    struct nn_cmsghdr *cmsg;
    size_t cmsgsz;

    iov.iov_base = (void*) msg;
    iov.iov_len = len;
    
    cmsgsz = NN_CMSG_SPACE (sizeof (msg_type));
    cmsg = nn_allocmsg (cmsgsz, 0);
    if (cmsg == NULL)
        return -1;

    cmsg->cmsg_level = NN_WS;
    cmsg->cmsg_type = NN_WS_MSG_TYPE;
    cmsg->cmsg_len = NN_CMSG_LEN (sizeof (msg_type));
    memcpy (NN_CMSG_DATA (cmsg), &msg_type, sizeof (msg_type));

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = &cmsg;
    hdr.msg_controllen = NN_MSG;

    rc = nn_sendmsg (s, &hdr, flags);

    return rc;
}

static int nn_ws_recv (int s, void *msg, size_t len, uint8_t *msg_type, int flags)
{
    struct nn_iovec iov;
    struct nn_msghdr hdr;
    struct nn_cmsghdr *cmsg;
    void *cmsg_buf;
    int rc;
    int frc;

    iov.iov_base = msg;
    iov.iov_len = len;

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = &cmsg_buf;
    hdr.msg_controllen = NN_MSG;

    rc = nn_recvmsg (s, &hdr, flags);
    if (rc < 0)
        return rc;

    /* Find WebSocket opcode ancillary property. */
    cmsg = NN_CMSG_FIRSTHDR (&hdr);
    while (cmsg) {
        if (cmsg->cmsg_level == NN_WS && cmsg->cmsg_type == NN_WS_MSG_TYPE) {
            *msg_type = *(uint8_t *) NN_CMSG_DATA (cmsg);
            break;
        }
        cmsg = NN_CMSG_NXTHDR (&hdr, cmsg);
    }

    /*  WebSocket transport should always report this header. */
    nn_assert (cmsg);

    frc = nn_freemsg (cmsg_buf);
    errno_assert (frc == 0);

    return rc;
}

static void nn_ws_fuzzing_client (NN_UNUSED void *args)
{
    FILE *fd;
    int rc;

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
        "                    \"protocols\" : [\"pair.sp.nanomsg.org\"]\n"
        "                  }\n"
        "               ],\n"
        "    \"outdir\" : \"./autobahnreports/client\",\n"
        "    \"cases\" : [\"*\"],\n"
        "    \"exclude-cases\" : %s,\n"
        "    \"exclude-agent-cases\" : {}\n"
        "}\n",
        FUZZING_CLIENT_ADDRESS, NN_WS_EXCLUDE_CASES);

    errno_assert (rc > 0);
    rc = fclose (fd);
    errno_assert (rc == 0);

    rc = system (
        "wstest"
        NN_WS_DEBUG_AUTOBAHN_FLAG
        " --mode=fuzzingclient "
        " --spec=fuzzingclient.json");
    errno_assert (rc == 0);

    return;
}

static void nn_ws_fuzzing_server (NN_UNUSED void)
{
    FILE *fd;
    int rc;

    /*  Create an Autobahn json config file in the same working directory
        as where the call to wstest will be. */
    fd = fopen ("fuzzingserver.json", "w+");
    errno_assert (fd != NULL);
    rc = fprintf (fd,
        "{\n"
        "    \"url\": \"%s\",\n"
        "    \"protocols\" : [\"pair.sp.nanomsg.org\"],\n"
        "    \"outdir\" : \"./autobahnreports/server\",\n"
        "    \"cases\" : [\"*\"],\n"
        "    \"exclude-cases\" : %s,\n"
        "    \"exclude-agent-cases\" : {}\n"
        "}\n",
        FUZZING_SERVER_ADDRESS, NN_WS_EXCLUDE_CASES);

    errno_assert (rc > 0);
    rc = fclose (fd);
    errno_assert (rc == 0);

    rc = system (
        "wstest"
        NN_WS_DEBUG_AUTOBAHN_FLAG
        " --mode=fuzzingserver"
        " --spec=fuzzingserver.json"
        " --webport=0");
    errno_assert (rc == 0);

    return;
}

static int nn_autobahn_test_case (int s, const char *method, int case_number)
{
    char addr [128];
    int ep;

    memset (addr, 0, sizeof (addr));
    sprintf (addr, "%s/%s?agent=nanomsg&case=%d", FUZZING_SERVER_ADDRESS,
        method, case_number);

    ep = test_connect (s, addr);

    return ep;
}

static int nn_autobahn_request (int s, const char *uri_path)
{
    char addr [128];
    int ep;

    memset (addr, 0, sizeof (addr));
    sprintf (addr, "%s/%s", FUZZING_SERVER_ADDRESS, uri_path);

    ep = test_connect (s, addr);

    return ep;
}

static void nn_ws_test_agent (void *arg)
{
    int s;
    int rc;
    uint8_t ws_msg_type;
    void *recv_buf;

    nn_assert (arg);

    s = *((int *) arg);

    /*  Remain active until socket is closed. */
    while (1) {

        ws_msg_type = 0;
        recv_buf = NULL;

        rc = nn_ws_recv (s, &recv_buf, NN_MSG, &ws_msg_type, 0);
        if (rc < 0) {
            errno_assert (errno == EBADF || errno == EINTR);
            return;
        }
        errno_assert (rc >= 0);

        switch (ws_msg_type) {
            
        /*  Echo text message verbatim. */
        case NN_WS_MSG_TYPE_TEXT:
            rc = nn_ws_send (s, &recv_buf, NN_MSG, ws_msg_type, 0);
            break;

        /*  Echo binary message verbatim. */
        case NN_WS_MSG_TYPE_BINARY:
            rc = nn_ws_send (s, &recv_buf, NN_MSG, ws_msg_type, 0);
            break;
            
        /*  As per RFC 6455 5.5.3, echo PING data payload as a PONG. Note
            that even though this ability is tested to satisfy as complete
            Autobahn test coverage as possible, it is neither recommended
            nor officially supported by nanomsg to use these opcodes, hence
            the use of "magic" numbers here. */
        case 0x09:
            rc = nn_ws_send (s, &recv_buf, NN_MSG, 0x0A, 0);
            break;
        
        /*  Silently ignore PONGs in this echo server. */
        case 0x0A:
            break;

        default:
            /*  The library delivered an unexpected message type. */
            nn_assert (0);
            break;
        }

        if (rc < 0) {
            errno_assert (errno == EBADF);
            return;
        }
        errno_assert (rc >= 0);
    }
    return;
}

int nn_ws_check_result (int case_num, const char *result, size_t len)
{
    /*  This is currently the exhaustive dictonary of responses potentially
        returned by Autobahn Testsuite v0.7.2. It is intentionally hard-coded,
        such that if the Autobahn dependency ever changes in any way, this
        test will likewise require re-evaluation. */
    const char *OK = "{\"behavior\": \"OK\"}";
    const char *NON = "{\"behavior\": \"NON-STRICT\"}";
    const char *INFO = "{\"behavior\": \"INFORMATIONAL\"}";
    const char *UNIMP = "{\"behavior\": \"UNIMPLEMENTED\"}";
    const char *FAILED = "{\"behavior\": \"FAILED\"}";
    int rc;

    if (strncmp (result, OK, len) == 0)
        rc = 0;
    else if (strncmp (result, NON, len) == 0)
        rc = 0;
    else if (strncmp (result, INFO, len) == 0)
        rc = 0;
    else if (strncmp (result, UNIMP, len) == 0)
        rc = -1;
    else if (strncmp (result, FAILED, len) == 0)
        rc = -1;
    else
        nn_assert (0);

    return rc;
}

int main ()
{
    int client_under_test;
    int client_under_test_ep;
    int server_under_test;
    int server_under_test_ep;
    int test_executive;
    int test_executive_ep;
    int msg_type;
    int rc;
    int i;
    int cases;
    int timeo;
    int opt;
    size_t sz;
    int passes;
    int failures;
    uint8_t ws_msg_type;
    uint8_t *recv_buf = NULL;
    struct nn_thread autobahn_server;
    struct nn_thread autobahn_client;
    struct nn_thread echo_agent;

    test_executive = test_socket (AF_SP, NN_PAIR);

    /*  Autobahn TestSuite always sends UTF-8. */
    msg_type = NN_WS_MSG_TYPE_TEXT;
    test_setsockopt (test_executive, NN_WS, NN_WS_MSG_TYPE, &msg_type,
        sizeof (msg_type));

    /*  The first receive could take a few seconds while Autobahn loads. */
    nn_thread_init (&autobahn_server, nn_ws_fuzzing_server, NULL);
    timeo = 10000;
    test_setsockopt (test_executive, NN_SOL_SOCKET, NN_RCVTIMEO, &timeo,
        sizeof (timeo));

    /*  Allow the server some time to initialize; else, the initial
        connections to it will fail. */
    nn_sleep (3000);

    /*  We expect nominally three ASCII digits [0-9] representing total
        number of cases to run as of Autobahn TestSuite v0.7.2, but anything
        between 1-4 digits is accepted. */
    test_executive_ep = nn_autobahn_request (test_executive, "getCaseCount");
    rc = nn_ws_recv (test_executive, &recv_buf, NN_MSG, &ws_msg_type, 0);
    errno_assert (1 <= rc && rc <= 4);
    nn_assert (ws_msg_type == NN_WS_MSG_TYPE_TEXT);
    test_shutdown (test_executive, test_executive_ep);

    /*  Parse ASCII response. */
    cases = 0;
    for (i = 0; i < rc; ++i) {
        nn_assert ('0' <= recv_buf [i] && recv_buf [i] <= '9');
        cases *= 10;
        cases += recv_buf [i] - '0';
    }

    rc = nn_freemsg (recv_buf);
    errno_assert (rc == 0);

    timeo = NN_WS_TEST_CASE_TIMEO;
    test_setsockopt (test_executive, NN_SOL_SOCKET, NN_RCVTIMEO, &timeo,
        sizeof (timeo));

    passes = 0;
    failures = 0;

    /*  Autobahn test cases are 1-indexed, not 0-indexed. */
    for (i = 1; i <= cases; i++) {
        /*  Register the Test Executive to listen for result from Autobahn. */
        test_executive_ep = nn_autobahn_test_case (test_executive, "getCaseStatus", i);

        /*  Prepare the echo client for Autobahn Fuzzing Server test case. */
        client_under_test = test_socket (AF_SP, NN_PAIR);
        opt = NN_WS_TEST_CASE_RCVMAX;
        sz = sizeof (opt);
        test_setsockopt (client_under_test, NN_SOL_SOCKET, NN_RCVMAXSIZE, &opt, sz);
        nn_thread_init (&echo_agent, nn_ws_test_agent, &client_under_test);

        /*  Launch test case on Autobahn Fuzzing Server. */
        client_under_test_ep = nn_autobahn_test_case (client_under_test, "runCase", i);

        /*  Wait for Autobahn Server to notify test case is complete. */
        rc = nn_ws_recv (test_executive, &recv_buf, NN_MSG, &ws_msg_type, 0);
        errno_assert (rc > 0);
        nn_assert (ws_msg_type == NN_WS_MSG_TYPE_TEXT);

        switch (nn_ws_check_result (i, (const char *)recv_buf, rc)) {
        case 0:
            passes++;
            break;
        case -1:
            failures++;
            break;
        default:
            nn_assert (0);
            break;
        }

        rc = nn_freemsg (recv_buf);
        errno_assert (rc == 0);
        test_close (client_under_test);
        test_shutdown (test_executive, test_executive_ep);
        nn_thread_term (&echo_agent);
    }
    
    printf ("Server test complete:\n"
            "Passes: %d\n"
            "Failures: %d\n",
            passes, failures);

    /*  Notify Autobahn it's time to create reports and shut down. */
    test_executive_ep = nn_autobahn_request (test_executive,
        "updateReports?agent=nanomsg&shutdownOnComplete=yes");

    nn_assert (failures == 0);

    /*  libnanomsg WebSocket Server testing by the Autobahn Client Fuzzer
        currently does not programmatically determine pass/failure of tests.
        The value of running this test remains, since even though it is not
        checking RFC 6455 compliance as reported from Autobahn, it is fully
        flexing its own assertions. */
    server_under_test = test_socket (AF_SP, NN_PAIR);
    opt = NN_WS_TEST_CASE_RCVMAX;
    sz = sizeof (opt);
    test_setsockopt (server_under_test, NN_SOL_SOCKET, NN_RCVMAXSIZE, &opt, sz);
    server_under_test_ep = test_bind (server_under_test, FUZZING_CLIENT_ADDRESS);
    nn_thread_init (&autobahn_client, nn_ws_fuzzing_client, NULL);
    nn_thread_init (&echo_agent, nn_ws_test_agent, &server_under_test);
    nn_thread_term (&autobahn_client);
    test_shutdown (server_under_test, server_under_test_ep);
    test_close (server_under_test);
    nn_thread_term (&echo_agent);
    
    /*  The client testing is expected to have output all reports by now. */
    test_shutdown (test_executive, test_executive_ep);
    test_close (test_executive);
    nn_thread_term (&autobahn_server);

    return 0;
}
