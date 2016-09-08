/*
    Copyright 2016 Garrett D'Amore <garrett@damore.org>

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
#include "../src/pubsub.h"

#include "testutil.h"

int main (int argc, const char *argv[])
{
    int sb;
    int id;
    int rv;
    char addr[128];

    test_addr_from(addr, "tcp", "123.45.67.89", get_test_port(argc, argv));

    sb = test_socket (AF_SP, NN_PUB);
    id = nn_bind (sb, addr);
    if (id != 0) {
       /* This is ok, its not a valid address */
       return (0);
    }

    nn_sleep(100);
    test_send (sb, "ABC");
    nn_sleep(100);
    rv = nn_shutdown (sb, id);
    nn_assert (rv == 0);
    nn_sleep(100);
    test_close (sb);

    return 0;
}

