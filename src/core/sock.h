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

#ifndef SP_SOCK_INCLUDED
#define SP_SOCK_INCLUDED

struct sp_sock;
struct sp_pipe;

/*  Deallocate the socket. */
void sp_sock_term (struct sp_sock *self);

/*  Set a socket option. */
int sp_sock_setopt (struct sp_sock *self, int level, int option,
    const void *optval, size_t optvallen); 

/*  Retrieve a socket option. */
int sp_sock_getopt (struct sp_sock *self, int level, int option,
    void *optval, size_t *optvallen);

/*  Send a message to the socket. */
int sp_sock_send (struct sp_sock *self, const void *buf,
    size_t len, int flags);

/*  Receive a message from the socket. */
int sp_sock_recv (struct sp_sock *self, void *buf, size_t *len, int flags);

int sp_sock_fd (struct sp_sock *self);
int sp_sock_add (struct sp_sock *self, struct sp_pipe *pipe);
void sp_sock_rm (struct sp_sock *self, struct sp_pipe *pipe);
void sp_sock_in (struct sp_sock *self, struct sp_pipe *pipe);
void sp_sock_out (struct sp_sock *self, struct sp_pipe *pipe);

#endif
