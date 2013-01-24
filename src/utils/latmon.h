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

#ifndef NN_LATMON_INCLUDED
#define NN_LATMON_INCLUDED

#if NN_LATENCY_MONITOR

#define NN_LATMON_SEND 0
#define NN_LATMON_SEND_TO_TRANSPORT 1
#define NN_LATMON_SEND_TO_PEER 2
#define NN_LATMON_RECV_FROM_PEER 3
#define NN_LATMON_RECV_FROM_TRANSPORT 4
#define NN_LATMON_RECV 5

#define NN_LATMON_EVENT_COUNT 6

void nn_latmon_init (void);
void nn_latmon_term (void);

void nn_latmon_measure (int event);

#endif

#endif
