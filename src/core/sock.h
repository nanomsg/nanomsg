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
struct sp_cp;

/*  Called by sp_term() to let the socket know about the process shutdown. */
void sp_sock_zombify (struct sp_sock *self);

/*  Called by sp_close(). */
void sp_sock_close (struct sp_sock *self);

/*  Returns default completion port associated with the socket. */
struct sp_cp *sp_sock_getcp (struct sp_sock *self);

/*  Set a socket option. */
int sp_sock_setopt (struct sp_sock *self, int level, int option,
    const void *optval, size_t optvallen); 

/*  Retrieve a socket option. If 'ignoreeterm' is set to 1 the option value
    can be retrieved even though sp_term() was already called. */
int sp_sock_getopt (struct sp_sock *self, int level, int option,
    void *optval, size_t *optvallen, int ignoreeterm);

/*  Add new endpoint to the socket. */
int sp_sock_create_ep (struct sp_sock *self, const char *addr,
    int (*factory) (const char *addr, void *hint, struct sp_epbase **ep));

/*  Remove the endpoint with the specified ID from the socket. */
int sp_sock_shutdown (struct sp_sock *self, int eid);

/*  used by endpoint to notify the socket that it has terminated. */
void sp_sock_ep_closed (struct sp_sock *self, struct sp_epbase *ep);

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
