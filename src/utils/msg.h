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

#ifndef SP_MSG_INCLUDED
#define SP_MSG_INCLUDED

#include "chunkref.h"

#include <stddef.h>

struct sp_msg {

    /*  Contains SP protocol message header. */
    struct sp_chunkref hdr;

    /*  Contains application level message payload. */
    struct sp_chunkref body;
};

/*  Initialises a message with body 'size' bytes long and empty header. */
void sp_msg_init (struct sp_msg *self, size_t size);

/*  TODO: This function is a termporary scaffolding.
    Should be removed later on. */
void sp_msg_init_data (struct sp_msg *self, const void *hdr, size_t hdrlen,
    const void *body, size_t bodylen);

/*  Frees resources allocate with the message. */
void sp_msg_term (struct sp_msg *self);

/*  Moves the content of the message from src to dst. dst should not be
    initialised prior to the operation. dst will be uninitialised after the
    operation. */
void sp_msg_mv (struct sp_msg *dst, struct sp_msg *src);

/*  Copies a message from src to dst. dst should not be
    initialised prior to the operation. */
void sp_msg_cp (struct sp_msg *dst, struct sp_msg *src);

#endif

