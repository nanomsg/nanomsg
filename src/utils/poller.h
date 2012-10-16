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

#ifndef SP_POLLER_INCLUDED
#define SP_POLLER_INCLUDED

#include "config.h"

#include <stdint.h>

/*  Poller object virtualises different polling mechanism that signal whether
    a socket is ready for sending and/or receiving. It's not meant to virtualise
    polling mechanisms that signal I/O operation completaion (IOCP). */

struct sp_poller;

struct sp_poller_vfptr {
    void (*in) (struct sp_poller *self);
    void (*out) (struct sp_poller *self);
    void (*err) (struct sp_poller *self);
};

void sp_poller_ctx_init (void);
void sp_poller_ctx_term (void);

void sp_poller_init (struct sp_poller *self,
    struct sp_poller_vfptr *vfptr, int fd);
void sp_poller_term (struct sp_poller *self);

void sp_poller_set_in (struct sp_poller *self);
void sp_poller_reset_in (struct sp_poller *self);
void sp_poller_set_out (struct sp_poller *self);
void sp_poller_reset_out (struct sp_poller *self);

#if defined SP_USE_POLL

struct sp_poller {
    struct sp_poller_vfptr *vfptr;
    int index;
};

#elif defined SP_USE_EPOLL

struct sp_poller {
    struct sp_poller_vfptr *vfptr;
    int fd;
    uint32_t events;
};

#endif

#endif

