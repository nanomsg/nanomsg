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

#ifndef SP_MSG_INCLUDED
#define SP_MSG_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define SP_MAX_VSM_SIZE 30

#define SP_MSGTYPE_VSM 101
#define SP_MSGTYPE_LMSG 102

struct sp_content
{
    size_t size;
    /* Actual message data follow the 'size' field. */

};

/*  This class represents an SP message. */

struct sp_msg {
    union
    {
        struct {
            uint8_t unused [SP_MAX_VSM_SIZE + 1];
            uint8_t type;
        } base;
        struct {
            uint8_t data [SP_MAX_VSM_SIZE];
            uint8_t size;
            uint8_t type;
        } vsm;
        struct {
            struct sp_content *content;
            uint8_t unused [SP_MAX_VSM_SIZE + 1 - sizeof (struct sp_content*)];
            uint8_t type;
        } lmsg;
    };
};

/*  Initialise the message of the specified size. The content of the message
    is undefined. */
int sp_msg_init (struct sp_msg *self, size_t size);

/*  Initialise the message by taking over the content of the specified message.
    After the call the source message is deallocated. */
void sp_msg_init_move (struct sp_msg *self, struct sp_msg *src);

/*  Terminate the message. */
void sp_msg_term (struct sp_msg *self);

/*  Returns pointer to the message data. */
uint8_t *sp_msg_data (struct sp_msg *self);

/*  Returns size of the message data in bytes. */
size_t sp_msg_size (struct sp_msg *self);

#endif

