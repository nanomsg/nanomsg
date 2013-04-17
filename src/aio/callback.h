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

#ifndef NN_CALLBACK_INCLUDED
#define NN_CALLBACK_INCLUDED

/*  Base class for objects accepting callbacks. */

struct nn_callback;

struct nn_callback_vfptr {

    /*  Virtual function to be implemented by the derived class to handle the
        callbacks. 'source' parameter is pointer to the object that generated
        the callback. As it can be any kind of object it is of type void*.
        The user should check whether the pointer points to any source of
        callbacks it is aware of and cast the pointer accordingly. If a single
        object can generate different kinds of callbacks, 'type' parameter
        specifies the callback type. Possible values are defined by the object
        that is the source of callbacks. */
    void (*callback) (struct nn_callback *self, void *source, int type);
};

struct nn_callback {
    const struct nn_callback_vfptr *vfptr;
};

void nn_callback_init (struct nn_callback *self,
    const struct nn_callback_vfptr *vfptr);
void nn_callback_term (struct nn_callback *self);

#endif

