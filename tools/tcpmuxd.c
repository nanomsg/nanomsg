/*
    Copyright (c) 2014 Martin Sustrik  All rights reserved.

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

#include "../src/utils/err.h"
#include "../src/utils/sleep.h"

#include <stdlib.h>
#include <stdio.h>

int main (int argc, const char *argv [])
{
    int rc;
    int port;

    /*  Parse the command line arguments. */
    if (argc != 2) {
        fprintf (stderr, "usage: tcpmuxd <port>\n");
        return 1;
    }

    /* TODO: Use strtol here to detect malformed port numbers. */
    port = atoi (argv [1]);

    /*  Start the daemon. */
    rc = nn_tcpmuxd (port);
    errno_assert (rc == 0);

    /*  Block the main thread forever so that deamon isn't killed
        immediately. */
    while (1) {
        nn_sleep (24 * 60 * 60 * 1000);
    }
}

