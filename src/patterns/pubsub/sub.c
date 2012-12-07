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

#include "sub.h"
#include "trie.h"

#include "../../sp.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/alloc.h"
#include "../../utils/excl.h"

struct sp_sub {
    struct sp_sockbase sockbase;
    struct sp_excl excl;
    struct sp_trie trie;
};

/*  Implementation of sp_sockbase's virtual functions. */
static void sp_sub_term (struct sp_sockbase *self);
static int sp_sub_add (struct sp_sockbase *self, struct sp_pipe *pipe);
static void sp_sub_rm (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sub_in (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sub_out (struct sp_sockbase *self, struct sp_pipe *pipe);
static int sp_sub_send (struct sp_sockbase *self, const void *buf, size_t len);
static int sp_sub_recv (struct sp_sockbase *self, void *buf, size_t *len);
static int sp_sub_setopt (struct sp_sockbase *self, int option,
    const void *optval, size_t optvallen);
static int sp_sub_getopt (struct sp_sockbase *self, int option,
    void *optval, size_t *optvallen);
static const struct sp_sockbase_vfptr sp_sub_sockbase_vfptr = {
    sp_sub_term,
    sp_sub_add,
    sp_sub_rm,
    sp_sub_in,
    sp_sub_out,
    sp_sub_send,
    sp_sub_recv,
    sp_sub_setopt,
    sp_sub_getopt
};

void sp_sub_init (struct sp_sub *self, const struct sp_sockbase_vfptr *vfptr,
    int fd)
{
    sp_sockbase_init (&self->sockbase, vfptr, fd);
    sp_excl_init (&self->excl);
    sp_trie_init (&self->trie);
}

void sp_sub_term (struct sp_sockbase *self)
{
    struct sp_sub *sub;

    sub = sp_cont (self, struct sp_sub, sockbase);

    sp_trie_term (&sub->trie);
    sp_excl_term (&sub->excl);
}

static int sp_sub_add (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_add (&sp_cont (self, struct sp_sub, sockbase)->excl, pipe);
}

static void sp_sub_rm (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    sp_excl_rm (&sp_cont (self, struct sp_sub, sockbase)->excl, pipe);
}

static int sp_sub_in (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_in (&sp_cont (self, struct sp_sub, sockbase)->excl, pipe);
}

static int sp_sub_out (struct sp_sockbase *self, struct sp_pipe *pipe)
{
    return sp_excl_out (&sp_cont (self, struct sp_sub, sockbase)->excl, pipe);
}

static int sp_sub_send (struct sp_sockbase *self, const void *buf, size_t len)
{
    return sp_excl_send (&sp_cont (self, struct sp_sub, sockbase)->excl,
        buf, len);
}

static int sp_sub_recv (struct sp_sockbase *self, void *buf, size_t *len)
{
    int rc;
    struct sp_sub *sub;

    sub = sp_cont (self, struct sp_sub, sockbase);

    /*  Loop while a matching message is found or when there are no more
        messages to receive. */
    while (1) {
        rc = sp_excl_recv (&sub->excl, buf, len);
        if (sp_slow (rc == -EAGAIN))
            return -EAGAIN;
        errnum_assert (rc == 0, -rc);
        rc = sp_trie_match (&sub->trie, buf, *len);
        if (rc == 0)
            continue;
        if (rc == 1)
            return 0;
        errnum_assert (0, -rc);
    }
}

static int sp_sub_setopt (struct sp_sockbase *self, int option,
        const void *optval, size_t optvallen)
{
    int rc;
    struct sp_sub *sub;

    sub = sp_cont (self, struct sp_sub, sockbase);

    if (option == SP_SUBSCRIBE) {
        rc = sp_trie_subscribe (&sub->trie, optval, optvallen);
        if (rc >= 0)
            return 0;
        return rc;
    }

    if (option == SP_UNSUBSCRIBE) {
        rc = sp_trie_unsubscribe (&sub->trie, optval, optvallen);
        if (rc >= 0)
            return 0;
        return rc;
    }

    return -ENOPROTOOPT;
}

static int sp_sub_getopt (struct sp_sockbase *self, int option,
        void *optval, size_t *optvallen)
{
    return -ENOPROTOOPT;
}

static struct sp_sockbase *sp_sub_create (int fd)
{
    struct sp_sub *self;

    self = sp_alloc (sizeof (struct sp_sub), "socket (sub)");
    alloc_assert (self);
    sp_sub_init (self, &sp_sub_sockbase_vfptr, fd);
    return &self->sockbase;
}

static struct sp_socktype sp_sub_socktype_struct = {
    AF_SP,
    SP_SUB,
    sp_sub_create
};

struct sp_socktype *sp_sub_socktype = &sp_sub_socktype_struct;

