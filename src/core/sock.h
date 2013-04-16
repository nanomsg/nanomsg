/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#ifndef NN_SOCK_INCLUDED
#define NN_SOCK_INCLUDED

struct nn_sock;
struct nn_pipe;
struct nn_msg;
struct nn_cp;

/*  Called after the whole socket (including the derived class) is
    intialised. */
void nn_sock_postinit (struct nn_sock *self, int domain, int protocol);

/*  Called by nn_close() to deallocate the socket. It's a blocking function
    and can return -EINTR. */
int nn_sock_destroy (struct nn_sock *self);

/*  Called by nn_term() to let the socket know about the process shutdown. */
void nn_sock_zombify (struct nn_sock *self);

/*  Returns default completion port associated with the socket. */
struct nn_cp *nn_sock_getcp (struct nn_sock *self);

/*  Returns a worker. Each call to this function may return different worker. */
struct nn_worker *nn_sock_choose_worker (struct nn_sock *self);

/*  Returns 1 if the specified socket type is a valid peer for this socket,
    0 otherwise. */
int nn_sock_ispeer (struct nn_sock *self, int socktype);

/*  Add new endpoint to the socket. */
int nn_sock_add_ep (struct nn_sock *self, const char *addr,
    int (*factory) (const char *addr, void *hint, struct nn_epbase **ep));

/*  Remove the endpoint with the specified ID from the socket. */
int nn_sock_rm_ep (struct nn_sock *self, int eid);

/*  used by endpoint to notify the socket that it has terminated. */
void nn_sock_ep_closed (struct nn_sock *self, struct nn_epbase *ep);

/*  Send a message to the socket. */
int nn_sock_send (struct nn_sock *self, struct nn_msg *msg, int flags);

/*  Receive a message from the socket. */
int nn_sock_recv (struct nn_sock *self, struct nn_msg *msg, int flags);

/*  Set a socket option. */
int nn_sock_setopt (struct nn_sock *self, int level, int option,
    const void *optval, size_t optvallen); 

/*  Retrieve a socket option. 'internal' is set to 1 when called from within
    nanomsg rather than from the API (nn_getsockopt). */
int nn_sock_getopt (struct nn_sock *self, int level, int option,
    void *optval, size_t *optvallen, int internal);

/*  Used by pipes. */
int nn_sock_add (struct nn_sock *self, struct nn_pipe *pipe);
void nn_sock_rm (struct nn_sock *self, struct nn_pipe *pipe);
void nn_sock_in (struct nn_sock *self, struct nn_pipe *pipe);
void nn_sock_out (struct nn_sock *self, struct nn_pipe *pipe);

#endif
