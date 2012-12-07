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

#include "inproc.h"
#include "inproc_ctx.h"
#include "msgpipe.h"

#include "../../utils/mutex.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

#include <string.h>

/*  sp_transport interface. */
static const char *sp_inproc_ctx_name (void);
static void sp_inproc_ctx_init (void);
static void sp_inproc_ctx_term (void);
static int sp_inproc_ctx_bind (const char *addr, void *hint,
    struct sp_epbase **epbase);
static int sp_inproc_ctx_connect (const char *addr, void *hint,
    struct sp_epbase **epbase);

static struct sp_transport sp_inproc_vfptr = {
    sp_inproc_ctx_name,
    sp_inproc_ctx_init,
    sp_inproc_ctx_term,
    sp_inproc_ctx_bind,
    sp_inproc_ctx_connect
};

struct sp_transport *sp_inproc = &sp_inproc_vfptr;

struct sp_inproc_ctx {

    /*  List of all bound inproc endpoints. */
    struct sp_list bound;

    /*  List of all connected inproc endpoints. */
    struct sp_list connected;

};

static struct sp_inproc_ctx self;

static const char *sp_inproc_ctx_name (void)
{
    return "inproc";
}

static void sp_inproc_ctx_init (void)
{
    sp_list_init (&self.bound);
    sp_list_init (&self.connected);
}
static void sp_inproc_ctx_term (void)
{
    sp_list_term (&self.connected);
    sp_list_term (&self.bound);
}

static int sp_inproc_ctx_bind (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_list_item *it;
    struct sp_inprocb *inprocb;
    struct sp_inprocc *inprocc;
    struct sp_msgpipe *pipe;

    /*  Check whether the endpoint isn't already bound. */
    /*  TODO:  This is an O(n) algorithm! */
    for (it = sp_list_begin (&self.bound); it != sp_list_end (&self.bound);
          it = sp_list_next (&self.bound, it)) {
        inprocb = sp_cont (it, struct sp_inprocb, list);
        if (strncmp (addr, inprocb->addr, SP_INPROCB_NAMELEN_MAX) == 0)
            return -EADDRINUSE;
    }

    /*  Insert the entry into the endpoint repository. */
    inprocb = sp_alloc (sizeof (struct sp_inprocb), "inprocb");
    alloc_assert (inprocb);
    rc = sp_inprocb_init (inprocb, addr, hint);
    if (sp_slow (rc != 0))
        return rc;
    sp_list_insert (&self.bound, &inprocb->list, sp_list_end (&self.bound));

    /*  During this process new pipes may be created. */
    for (it = sp_list_begin (&self.connected);
          it != sp_list_end (&self.connected);
          it = sp_list_next (&self.connected, it)) {
        inprocc = sp_cont (it, struct sp_inprocc, list);
        if (strncmp (addr, inprocc->addr, SP_INPROCC_NAMELEN_MAX) == 0) {
            pipe = sp_alloc (sizeof (struct sp_msgpipe), "msgpipe");
            alloc_assert (pipe);
            sp_msgpipe_init (pipe, inprocb, inprocc);
        }
    }

    sp_assert (epbase);
    *epbase = &inprocb->epbase;
    return 0;
}

static int sp_inproc_ctx_connect (const char *addr, void *hint,
    struct sp_epbase **epbase)
{
    int rc;
    struct sp_list_item *it;
    struct sp_inprocc *inprocc;
    struct sp_inprocb *inprocb;
    struct sp_msgpipe *pipe;

    /*  Insert the entry into the endpoint repository. */
    inprocc = sp_alloc (sizeof (struct sp_inprocc), "inprocc");
    alloc_assert (inprocc);
    rc = sp_inprocc_init (inprocc, addr, hint);
    if (sp_slow (rc != 0))
        return rc;
    sp_list_insert (&self.connected, &inprocc->list,
        sp_list_end (&self.connected));

    /*  During this process a pipe may be created. */
    for (it = sp_list_begin (&self.bound);
          it != sp_list_end (&self.bound);
          it = sp_list_next (&self.bound, it)) {
        inprocb = sp_cont (it, struct sp_inprocb, list);
        if (strcmp (addr, inprocb->addr) == 0) {
            pipe = sp_alloc (sizeof (struct sp_msgpipe), "msgpipe");
            alloc_assert (pipe);
            sp_msgpipe_init (pipe, inprocb, inprocc);
            break;
        }
    }

    sp_assert (epbase);
    *epbase = &inprocc->epbase;
    return 0;
}

void sp_inproc_ctx_unbind (struct sp_inprocb *inprocb)
{
    sp_list_erase (&self.bound, &inprocb->list);
}

void sp_inproc_ctx_disconnect (struct sp_inprocc *inprocc)
{
    sp_list_erase (&self.connected, &inprocc->list);
}

