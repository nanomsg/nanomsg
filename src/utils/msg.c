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

#include "msg.h"
#include "err.h"
#include "alloc.h"

int sp_msg_init (struct sp_msg *self, size_t size)
{
    /*  Small message. */
    if (size <= SP_MAX_VSM_SIZE) {
        self->vsm.type = SP_MSGTYPE_VSM;
        self->vsm.size = (uint8_t) size;
        return 0;
    }

    /*  Large message. */
    self->lmsg.type = SP_MSGTYPE_LMSG;
    self->lmsg.content = sp_alloc (sizeof (struct sp_content) + size);
    if (!self->lmsg.content)
        return -ENOMEM;
    self->lmsg.content->size = size;

    return 0;
}

void sp_msg_init_move (struct sp_msg *self, struct sp_msg *src)
{
    *self = *src;
    src->base.type = 0;
}

void sp_msg_term (struct sp_msg *self)
{
    /*  Small message. */
    if (self->base.type == SP_MSGTYPE_VSM) {
        self->base.type = 0;
        return;
    }

    /*  Large message. */
    sp_free (self->lmsg.content);
    self->base.type = 0;
}

uint8_t *sp_msg_data (struct sp_msg *self)
{
    /*  Small message. */
    if (self->base.type == SP_MSGTYPE_VSM)
        return self->vsm.data;

    /*  Large message. */
    return (uint8_t*) ((&self->lmsg.content->size) + 1);
}

size_t sp_msg_size (struct sp_msg *self)
{
    /*  Small message. */
    if (self->base.type == SP_MSGTYPE_VSM)
        return self->vsm.size;

    /*  Large message. */
    return self->lmsg.content->size;
}

