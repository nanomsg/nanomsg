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

#ifndef SP_IPCA_INCLUDED
#define SP_IPCA_INCLUDED

#if !defined SP_HAVE_WINDOWS

#include "ipcb.h"

/*  Let's re-use TCP session. On POSIX platforms, once created, there should
    be no difference between TCP and UNIX domain sockets. */
#include "../tcp/tcps.h"

#include "../../utils/aio.h"
#include "../../utils/list.h"

/*  Represents IPC accepted socket. */

struct sp_ipca {

    /*  Event sink. */
    const struct sp_cp_sink *sink;

    /*  The undelying IPC socket. */
    struct sp_usock usock;

    /*  IPC session state machine. */
    struct sp_tcps session;

    /*  ipcb object that created this connection. */
    struct sp_ipcb *ipcb;

    /*  The object is part of sp_ipcb's list of accepted sockets. */
    struct sp_list_item item;
};

void sp_ipca_init (struct sp_ipca *self, struct sp_epbase *epbase,
    int s, struct sp_usock *usock, struct sp_ipcb *ipcb);
int sp_ipca_close (struct sp_ipca *self);

#endif

#endif

