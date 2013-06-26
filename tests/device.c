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
#include "../src/bus.h"
#include "../src/pair.h"
#include "../src/fanout.h"
#include "../src/inproc.h"

#include "../src/utils/err.c"
#include "../src/utils/sleep.c"
#include "../src/utils/thread.c"

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

    /*  Intialise the device sockets. */
    deva = nn_socket (AF_SP_RAW, NN_PAIR);
    errno_assert (deva >= 0);
    rc = nn_bind (deva, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    devb = nn_socket (AF_SP_RAW, NN_PAIR);
    errno_assert (devb >= 0);
    rc = nn_bind (devb, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);

    /*  Run the device. */
    rc = nn_device (deva, devb);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    rc = nn_close (devb);
    errno_assert (rc == 0);
    rc = nn_close (deva);
    errno_assert (rc == 0);
}

void device2 (void *arg)
{
    int rc;
    int devc;
    int devd;

    /*  Intialise the device sockets. */
    devc = nn_socket (AF_SP_RAW, NN_PULL);
    errno_assert (devc >= 0);
    rc = nn_bind (devc, SOCKET_ADDRESS_C);
    errno_assert (rc >= 0);
    devd = nn_socket (AF_SP_RAW, NN_PUSH);
    errno_assert (devd >= 0);
    rc = nn_bind (devd, SOCKET_ADDRESS_D);
    errno_assert (rc >= 0);

    /*  Run the device. */
    rc = nn_device (devc, devd);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    rc = nn_close (devd);
    errno_assert (rc == 0);
    rc = nn_close (devc);
    errno_assert (rc == 0);
}

void device3 (void *arg)
{
    int rc;
    int deve;

    /*  Intialise the device socket. */
    deve = nn_socket (AF_SP_RAW, NN_BUS);
    errno_assert (deve >= 0);
    rc = nn_bind (deve, SOCKET_ADDRESS_E);
    errno_assert (rc >= 0);

    /*  Run the device. */
    rc = nn_device (deve, -1);
    nn_assert (rc < 0 && nn_errno () == ETERM);

    /*  Clean up. */
    rc = nn_close (deve);
    errno_assert (rc == 0);
}

int main ()
{
#if 0
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
    char buf [3];
    int timeo;

    /*  Test the bi-directional device. */

    /*  Start the device. */
    nn_thread_init (&thread1, device1, NULL);

    /*  Create two sockets to connect to the device. */
    enda = nn_socket (AF_SP, NN_PAIR);
    errno_assert (enda >= 0);
    rc = nn_connect (enda, SOCKET_ADDRESS_A);
    errno_assert (rc >= 0);
    endb = nn_socket (AF_SP, NN_PAIR);
    errno_assert (endb >= 0);
    rc = nn_connect (endb, SOCKET_ADDRESS_B);
    errno_assert (rc >= 0);

    /*  Pass a pair of messages between endpoints. */
    rc = nn_send (enda, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (endb, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_send (endb, "ABC", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (enda, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    /*  Clean up. */
    rc = nn_close (endb);
    errno_assert (rc == 0);
    rc = nn_close (enda);
    errno_assert (rc == 0);

    /*  Test the uni-directional device. */

    /*  Start the device. */
    nn_thread_init (&thread2, device2, NULL);

    /*  Create two sockets to connect to the device. */
    endc = nn_socket (AF_SP, NN_PUSH);
    errno_assert (endc >= 0);
    rc = nn_connect (endc, SOCKET_ADDRESS_C);
    errno_assert (rc >= 0);
    endd = nn_socket (AF_SP, NN_PULL);
    errno_assert (endd >= 0);
    rc = nn_connect (endd, SOCKET_ADDRESS_D);
    errno_assert (rc >= 0);

    /*  Pass a message between endpoints. */
    rc = nn_send (endc, "XYZ", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (endd, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    /*  Clean up. */
    rc = nn_close (endd);
    errno_assert (rc == 0);
    rc = nn_close (endc);
    errno_assert (rc == 0);

    /*  Test the loopback device. */

    /*  Start the device. */
    nn_thread_init (&thread3, device3, NULL);

    /*  Create two sockets to connect to the device. */
    ende1 = nn_socket (AF_SP, NN_BUS);
    errno_assert (ende1 >= 0);
    rc = nn_connect (ende1, SOCKET_ADDRESS_E);
    errno_assert (rc >= 0);
    ende2 = nn_socket (AF_SP, NN_BUS);
    errno_assert (ende2 >= 0);
    rc = nn_connect (ende2, SOCKET_ADDRESS_E);
    errno_assert (rc >= 0);

    /*  BUS is unreliable so wait a bit for connections to be established. */
    nn_sleep (100);

    /*  Pass a message to the bus. */
    rc = nn_send (ende1, "KLM", 3, 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);
    rc = nn_recv (ende2, buf, sizeof (buf), 0);
    errno_assert (rc >= 0);
    nn_assert (rc == 3);

    /*  Make sure that the message doesn't arrive at the socket it was
        originally sent to. */
    timeo = 100;
    rc = nn_setsockopt (ende1, NN_SOL_SOCKET, NN_RCVTIMEO,
       &timeo, sizeof (timeo));
    errno_assert (rc == 0);
    rc = nn_recv (ende1, buf, sizeof (buf), 0);
    errno_assert (rc < 0 && nn_errno () == EAGAIN);

    /*  Clean up. */
    rc = nn_close (ende2);
    errno_assert (rc == 0);
    rc = nn_close (ende1);
    errno_assert (rc == 0);

    /*  Shut down the devices. */
    nn_term ();
    nn_thread_term (&thread1);
    nn_thread_term (&thread2);
    nn_thread_term (&thread3);
#endif

    return 0;
}

