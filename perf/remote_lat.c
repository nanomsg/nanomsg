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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../src/utils/stopwatch.c"

int main (int argc, char *argv [])
{
    const char *connect_to;
    size_t sz;
    int rts;
    char *buf;
    int nbytes;
    int s;
    int rc;
    int i;
    struct sp_stopwatch sw;
    uint64_t total;
    double lat;
    

    if (argc != 4) {
        printf ("usage: remote_lat <connect-to> <msg-size> <roundtrips>\n");
        return 1;
    }
    connect_to = argv [1];
    sz = atoi (argv [2]);
    rts = atoi (argv [3]);

    rc = sp_init ();
    assert (rc == 0);
    s = sp_socket (AF_SP, SP_PAIR);
    assert (s != -1);
    rc = sp_connect (s, connect_to);
    assert (rc >= 0);

    buf = malloc (sz);
    assert (buf);
    memset (buf, 111, sz);

    sp_stopwatch_init (&sw);
    for (i = 0; i != rts; i++) {
        nbytes = sp_send (s, buf, sz, 0);
        assert (nbytes == sz);
        nbytes = sp_recv (s, buf, sz, 0);
        assert (nbytes == sz);
    }
    total = sp_stopwatch_term (&sw);

    lat = (double) total / (rts * 2);
    printf ("message size: %d [B]\n", (int) sz);
    printf ("roundtrip count: %d\n", (int) rts);
    printf ("average latency: %.3f [us]\n", (double) lat);

    free (buf);

    rc = sp_close (s);
    assert (rc == 0);

    rc = sp_term ();
    assert (rc == 0);

    return 0;
}
