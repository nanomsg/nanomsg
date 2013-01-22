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

#ifndef NN_EFD_INCLUDED
#define NN_EFD_INCLUDED

#if !defined NN_HAVE_WINDOWS

struct nn_efd;

void nn_efd_init (struct nn_efd *self);
void nn_efd_term (struct nn_efd *self);
int nn_efd_getfd (struct nn_efd *self);
void nn_efd_signal (struct nn_efd *self);
void nn_efd_unsignal (struct nn_efd *self);

#if defined NN_USE_SOCKETPAIR

struct nn_efd
{
    int r;
    int w;
};

#elif defined NN_USE_EVENTFD

struct nn_efd
{
    int efd;
};

#endif

#endif

#endif

