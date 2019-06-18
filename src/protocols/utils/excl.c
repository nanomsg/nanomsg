/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.

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

#include "excl.h"

#include "../../utils/fast.h"
#include "../../utils/err.h"
#include "../../utils/attr.h"
#include "../../utils/queue.h"
#include "../../transport.h"

#include <stdlib.h>

void nn_excl_init (struct nn_excl *self)
{
    self->pipe = NULL;
    self->inpipe = NULL;
    self->outpipe = NULL;
    self->outpipe_pipe = NULL;
}

void nn_excl_term (struct nn_excl *self)
{
    nn_assert (!self->pipe);
    nn_assert (!self->inpipe);
    nn_assert (!self->outpipe);
    nn_assert (!self->outpipe_pipe);
}

int nn_excl_add (struct nn_excl *self, struct nn_pipe *pipe)
{
    /*  If there's a connection being used, reject any new connection. */
    if (self->pipe)
        return -EISCONN;

    /*  Remember that this pipe is the active one. */
    self->pipe = pipe;

    return 0;
}

void nn_excl_rm (struct nn_excl *self, NN_UNUSED struct nn_pipe *pipe)
{
   nn_assert (self->pipe);
   self->pipe = NULL;
   self->inpipe = NULL;
   self->outpipe = NULL;
   self->outpipe_pipe = NULL;
}

void nn_excl_in (struct nn_excl *self, struct nn_pipe *pipe)
{
    nn_assert (!self->inpipe);
    nn_assert (pipe == self->pipe);
    self->inpipe = pipe;
}

extern const struct nn_pipebase_vfptr nn_stcp_pipebase_vfptr;
void nn_excl_out (struct nn_excl *self, struct nn_pipe *pipe)
{
    nn_assert (!self->outpipe);
    nn_assert (pipe == self->pipe);
    self->outpipe = pipe;
    struct nn_pipebase *pipebase = (struct nn_pipebase*) pipe;
    /* only tcp support the out queuing */
    if(pipebase->vfptr->send == nn_stcp_pipebase_vfptr.send)
    {
        self->outpipe_pipe = pipe;
    }
}

int nn_excl_send (struct nn_excl *self, struct nn_msg *msg)
{
    int rc;

    if (nn_slow (!self->outpipe))
    {
        if(self->outpipe_pipe)
        {
            struct nn_pipebase *pipebase = (struct nn_pipebase *)(self->outpipe_pipe);
            struct nn_msg *msg_cp = malloc(sizeof(struct nn_msg));
            nn_msg_mv (msg_cp, msg);
            nn_queue_item_init (&msg_cp->queue_item);
            nn_mutex_lock(&pipebase->out_msgs_mutex);
            if(pipebase->n_outmsgs >= NN_MAX_OUT_MSGS_IN_QUEUE)
            {
                nn_mutex_unlock(&pipebase->out_msgs_mutex);
                free(msg_cp);
                nn_err_log("ERROR", "EAGAIN: queue size %d", pipebase->n_outmsgs);
                return -EAGAIN;
            }
            nn_queue_push (&pipebase->out_msgs, &msg_cp->queue_item);
            pipebase->n_outmsgs++;
            nn_mutex_unlock(&pipebase->out_msgs_mutex);
            return 0;
        }
        return -EAGAIN;
    }

    rc = nn_pipe_send (self->outpipe, msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_PIPE_RELEASE)
        self->outpipe = NULL;

    return rc & ~NN_PIPE_RELEASE;
}

int nn_excl_recv (struct nn_excl *self, struct nn_msg *msg)
{
    int rc;

    if (nn_slow (!self->inpipe))
        return -EAGAIN;

    rc = nn_pipe_recv (self->inpipe, msg);
    errnum_assert (rc >= 0, -rc);

    if (rc & NN_PIPE_RELEASE)
        self->inpipe = NULL;

    return rc & ~NN_PIPE_RELEASE;
}

int nn_excl_can_send (struct nn_excl *self)
{
    return self->outpipe ? 1 : 0;
}

int nn_excl_can_recv (struct nn_excl *self)
{
    return self->inpipe ? 1 : 0;
}

