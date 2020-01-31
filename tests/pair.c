/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.

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

#include "testutil.h"

#define SOCKET_ADDRESS "tcp://0.0.0.0:8888"

int main (int argc, const char* argv[])
{
    int sb;
    int sc;
    int use_server = 0, use_client = 0;
    int n_send = 10000;
    int n_succ;
    if(argc == 1)
    {
        use_server = 1;
        use_client = 1;
    }
    else if(argc == 2)
    {
        if(strcmp("server", argv[1]) == 0)
        {
            use_server = 1;
            n_send = 4000000;
        }
        if(strcmp("client", argv[1]) == 0)
        {
            use_client = 1;
        }
    }
    else
    {
        printf("accepted server/client or no args at all\n");
        exit(-1);
    }
    
    if(use_server)
    {
        sb = test_socket (AF_SP, NN_PAIR);
        test_bind (sb, SOCKET_ADDRESS);
    }
    if(use_client)
    {
        sc = test_socket (AF_SP, NN_PAIR);
        test_connect (sc, SOCKET_ADDRESS);
        n_succ = 10000;
    }
    int n_err = 0;
    char *buf = "abcdefgaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\0";
    int len = strlen(buf);
    int i;
    if(use_server)
    {
        n_succ = 0;
        for (i = 0; i < n_send; i++)
        {
            int err = nn_send(sb, buf, len, NN_DONTWAIT);
            if(err <= 0)
            {
                n_err++;
                if(errno == -EAGAIN)
                {
                    // printf("%d: got eagain\n", i);
                } else
                {
                    // printf("%d: got err: %s\n", i, nn_err_strerror(errno));
                }
                
            } else
            {
                n_succ++;
            }
            
        }
        printf("send %d/%d done. err:%d\n", n_succ, i, n_err);
    }

    char r_buf[1000];
    n_err = 0;
    int n_succ_rx = 0;
    if(use_client)
    {
        for (i = 0; i < n_succ + 10; i++)
        {
            int err = nn_recv(sc, r_buf, sizeof(r_buf), NN_DONTWAIT);
            if(err <= 0)
            {
                n_err++;
                if(errno == -EAGAIN)
                {
                    // printf("%d: got eagain\n", i);
                } else
                {
                    // printf("%d: got err: %s\n", i, nn_err_strerror(errno));
                }
                
            } else
            {
                r_buf[err] = 0;
                if(strcmp(r_buf, buf) != 0)
                {
                    printf("%d: Compare failed exp:%s got:%s", i, buf, r_buf);
                }
                n_succ_rx++;
            }
            if(i == 357)
            {
                i = i / 0;
            }
        }
        printf("recv %d done. err: %d\n", n_succ_rx, n_err);
    }

    if(use_server)
    {
        test_close (sc);
    }
    if(use_client)
    {
        test_close (sb);
    }

    return 0;
}

