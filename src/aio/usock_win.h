/*  
    Copyright (c) 2013 250bpm s.r.o.

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

#include "fsm.h"

#include "../utils/win.h"

struct nn_usock;

#define NN_USOCK_OP_STATE_IDLE 1
#define NN_USOCK_OP_STATE_ACTIVE 2
#define NN_USOCK_OP_STATE_DONE 3
#define NN_USOCK_OP_STATE_ERROR 4

struct nn_usock_op {
    struct nn_usock *usock;
    int state;
    OVERLAPPED olpd;
};

struct nn_usock {

    /*  The state machine. */
    struct nn_fsm fsm;
    int state;

    /*  The actual underlying socket. */
    SOCKET s;

    /*  Handles for asynchronous operations being executed. */
    struct nn_usock_op in;
    struct nn_usock_op out;

    /*  When accepting new socket, they have to be created with same
        type as the listening socket. Thus, in listening socket we
        have to store its exact type. */
    int domain;
    int type;
    int protocol;

    /*  When accepting a new connection, the pointer to the object to associate
        the new connection with is stored here. */
    struct nn_usock *newsock;

    /*  Events raised by the usock. */ 
    struct nn_fsm_event event_established;
    struct nn_fsm_event event_sent;
    struct nn_fsm_event event_received;
    struct nn_fsm_event event_error;
};
