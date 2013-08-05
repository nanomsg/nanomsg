/*
    Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.

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

#include "sub.h"
#include "trie.h"

#include "../../nn.h"
#include "../../pubsub.h"

#include "../utils/excl.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"

struct nn_sub {
    struct nn_sockbase sockbase;
    struct nn_excl excl;
    struct nn_trie trie;
};

/*  Private functions. */
static void nn_sub_init (struct nn_sub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint);
static void nn_sub_term (struct nn_sub *self);

/*  Implementation of nn_sockbase's virtual functions. */
static void nn_sub_destroy (struct nn_sockbase *self);
static int nn_sub_add (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_sub_rm (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_sub_in (struct nn_sockbase *self, struct nn_pipe *pipe);
static void nn_sub_out (struct nn_sockbase *self, struct nn_pipe *pipe);
static int nn_sub_events (struct nn_sockbase *self);
static int nn_sub_recv (struct nn_sockbase *self, struct nn_msg *msg);
static int nn_sub_setopt (struct nn_sockbase *self, int level, int option,
    const void *optval, size_t optvallen);
static int nn_sub_getopt (struct nn_sockbase *self, int level, int option,
    void *optval, size_t *optvallen);
static const struct nn_sockbase_vfptr nn_sub_sockbase_vfptr = {
    NULL,
    nn_sub_destroy,
    nn_sub_add,
    nn_sub_rm,
    nn_sub_in,
    nn_sub_out,
    nn_sub_events,
    NULL,
    nn_sub_recv,
    nn_sub_setopt,
    nn_sub_getopt
};

static void nn_sub_init (struct nn_sub *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    nn_sockbase_init (&self->sockbase, vfptr, hint);
    nn_excl_init (&self->excl);
    nn_trie_init (&self->trie);
}

static void nn_sub_term (struct nn_sub *self)
{
    nn_trie_term (&self->trie);
    nn_excl_term (&self->excl);
    nn_sockbase_term (&self->sockbase);
}

void nn_sub_destroy (struct nn_sockbase *self)
{
    struct nn_sub *sub;

    sub = nn_cont (self, struct nn_sub, sockbase);

    nn_sub_term (sub);
    nn_free (sub);
}

static int nn_sub_add (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    return nn_excl_add (&nn_cont (self, struct nn_sub, sockbase)->excl, pipe);
}

static void nn_sub_rm (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_rm (&nn_cont (self, struct nn_sub, sockbase)->excl, pipe);
}

static void nn_sub_in (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_in (&nn_cont (self, struct nn_sub, sockbase)->excl, pipe);
}

static void nn_sub_out (struct nn_sockbase *self, struct nn_pipe *pipe)
{
    nn_excl_out (&nn_cont (self, struct nn_sub, sockbase)->excl, pipe);
}

static int nn_sub_events (struct nn_sockbase *self)
{
    return nn_excl_can_recv (&nn_cont (self, struct nn_sub, sockbase)->excl) ?
        NN_SOCKBASE_EVENT_IN : 0;
}

static int nn_sub_recv (struct nn_sockbase *self, struct nn_msg *msg)
{
    int rc;
    struct nn_sub *sub;

    sub = nn_cont (self, struct nn_sub, sockbase);

    /*  Loop while a matching message is found or when there are no more
        messages to receive. */
    while (1) {
        rc = nn_excl_recv (&sub->excl, msg);
        if (nn_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc >= 0, -rc);
        rc = nn_trie_match (&sub->trie, nn_chunkref_data (&msg->body),
            nn_chunkref_size (&msg->body));
        if (rc == 0)
            continue;
        if (rc == 1)
            return 0;
        errnum_assert (0, -rc);
    }
}

static int nn_sub_setopt (struct nn_sockbase *self, int level, int option,
        const void *optval, size_t optvallen)
{
    int rc;
    struct nn_sub *sub;

    sub = nn_cont (self, struct nn_sub, sockbase);

    if (level != NN_SUB)
        return -ENOPROTOOPT;

    if (option == NN_SUB_SUBSCRIBE) {
        rc = nn_trie_subscribe (&sub->trie, optval, optvallen);
        if (rc >= 0)
            return 0;
        return rc;
    }

    if (option == NN_SUB_UNSUBSCRIBE) {
        rc = nn_trie_unsubscribe (&sub->trie, optval, optvallen);
        if (rc >= 0)
            return 0;
        return rc;
    }

    return -ENOPROTOOPT;
}

static int nn_sub_getopt (struct nn_sockbase *self, int level, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static int nn_sub_create (void *hint, struct nn_sockbase **sockbase)
{
    struct nn_sub *self;

    self = nn_alloc (sizeof (struct nn_sub), "socket (sub)");
    alloc_assert (self);
    nn_sub_init (self, &nn_sub_sockbase_vfptr, hint);
    *sockbase = &self->sockbase;

    return 0;
}

static int nn_sub_ispeer (int socktype)
{
    return socktype == NN_PUB ? 1 : 0;
}

static struct nn_socktype nn_sub_socktype_struct = {
    AF_SP,
    NN_SUB,
    NN_SOCKTYPE_FLAG_NOSEND,
    nn_sub_create,
    nn_sub_ispeer,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_socktype *nn_sub_socktype = &nn_sub_socktype_struct;

