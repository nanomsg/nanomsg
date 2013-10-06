/*
    Copyright (c) 2012 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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
#include "../src/bus.h"
#include "../src/pair.h"
#include "../src/pipeline.h"
#include "../src/inproc.h"

#include "testutil.h"
#include "../src/utils/thread.c"
#include "../src/utils/sem.h"

#define SOCKET_ADDRESS_A "inproc://a"
#define SOCKET_ADDRESS_B "inproc://b"
#define SOCKET_ADDRESS_C "inproc://c"
#define SOCKET_ADDRESS_D "inproc://d"
#define SOCKET_ADDRESS_E "inproc://e"

void device1 (void *arg)
{
    int rc;
    int deva;
    int devb;
    struct nn_sem * semp = (struct nn_sem *)arg;

    /*  Intialise the device sockets. */
    deva = test_socket (AF_SP_RAW, NN_PAIR);
    test_bind (deva, SOCKET_ADDRESS_A);
    devb = test_socket (AF_SP_RAW, NN_PAIR);
    test_bind (devb, SOCKET_ADDRESS_B);

    /*  Notify others that bind succeeded */
    nn_sem_post (semp);

    /*  Run the device. */
    rc = nn_device (deva, devb);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    test_close (devb);
    test_close (deva);
}

void device2 (void *arg)
{
    int rc;
    int devc;
    int devd;
    struct nn_sem * semp = (struct nn_sem *)arg;

    /*  Intialise the device sockets. */
    devc = test_socket (AF_SP_RAW, NN_PULL);
    test_bind (devc, SOCKET_ADDRESS_C);
    devd = test_socket (AF_SP_RAW, NN_PUSH);
    test_bind (devd, SOCKET_ADDRESS_D);

    /*  Notify others that bind succeeded */
    nn_sem_post (semp);

    /*  Run the device. */
    rc = nn_device (devc, devd);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    test_close (devd);
    test_close (devc);
}

void device3 (void *arg)
{
    int rc;
    int deve;
    struct nn_sem * semp = (struct nn_sem *)arg;

    /*  Intialise the device socket. */
    deve = test_socket (AF_SP_RAW, NN_BUS);
    test_bind (deve, SOCKET_ADDRESS_E);

    /*  Notify others that bind succeeded */
    nn_sem_post (semp);

    /*  Run the device. */
    rc = nn_device (deve, -1);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    test_close (deve);
}

int main ()
{
    int rc;
    int enda;
    int endb;
    int endc;
    int endd;
    int ende1;
    int ende2;
    struct nn_thread thread1;
    struct nn_thread thread2;
    struct nn_thread thread3;
    struct nn_sem sem;
    char buf [3];
    int timeo;

    /*  Init semaphore */
    nn_sem_init (&sem);

    /*  Test the bi-directional device. */

    /*  Start the device. */
    nn_thread_init (&thread1, device1, &sem);

    /*  Create two sockets to connect to the device. */
    enda = test_socket (AF_SP, NN_PAIR);
    test_connect (enda, SOCKET_ADDRESS_A);
    endb = test_socket (AF_SP, NN_PAIR);
    test_connect (endb, SOCKET_ADDRESS_B);

    /*  Make sure we are not sending data before the socket has been bound */
    nn_sem_wait (&sem);

    /*  Pass a pair of messages between endpoints. */
    test_send (enda, "ABC");
    test_recv (endb, "ABC");
    test_send (endb, "ABC");
    test_recv (enda, "ABC");

    /*  Clean up. */
    test_close (endb);
    test_close (enda);

    /*  Test the uni-directional device. */

    /*  Start the device. */
    nn_thread_init (&thread2, device2, &sem);

    /*  Create two sockets to connect to the device. */
    endc = test_socket (AF_SP, NN_PUSH);
    test_connect (endc, SOCKET_ADDRESS_C);
    endd = test_socket (AF_SP, NN_PULL);
    test_connect (endd, SOCKET_ADDRESS_D);

    /*  Make sure we are not sending data before the socket has been bound */
    nn_sem_wait (&sem);

    /*  Pass a message between endpoints. */
    test_send (endc, "XYZ");
    test_recv (endd, "XYZ");

    /*  Clean up. */
    test_close (endd);
    test_close (endc);

    /*  Test the loopback device. */

    /*  Start the device. */
    nn_thread_init (&thread3, device3, &sem);

    /*  Create two sockets to connect to the device. */
    ende1 = test_socket (AF_SP, NN_BUS);
    test_connect (ende1, SOCKET_ADDRESS_E);
    ende2 = test_socket (AF_SP, NN_BUS);
    test_connect (ende2, SOCKET_ADDRESS_E);

    /*  BUS is unreliable so wait a bit for connections to be established. */
    nn_sleep (100);

    /*  Make sure we are not sending data before the socket has been bound */
    nn_sem_wait (&sem);

    /*  Pass a message to the bus. */
    test_send (ende1, "KLM");
    test_recv (ende2, "KLM");

    /*  Make sure that the message doesn't arrive at the socket it was
        originally sent to. */
    timeo = 100;
    rc = nn_setsockopt (ende1, NN_SOL_SOCKET, NN_RCVTIMEO,
       &timeo, sizeof (timeo));
    errno_assert (rc == 0);
    rc = nn_recv (ende1, buf, sizeof (buf), 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);

    /*  Clean up. */
    test_close (ende2);
    test_close (ende1);

    /*  Shut down the devices. */
    nn_term ();
    nn_thread_term (&thread1);
    nn_thread_term (&thread2);
    nn_thread_term (&thread3);

    return 0;
}

