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

#include "inproc.h"
#include "inproc_ctx.h"
#include "msgpipe.h"

#include "../../inproc.h"

#include "../../utils/mutex.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

#include <string.h>

/*  nn_transport interface. */
static void nn_inproc_ctx_init (void);
static void nn_inproc_ctx_term (void);
static int nn_inproc_ctx_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_inproc_ctx_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);

static struct nn_transport nn_inproc_vfptr = {
    "inproc",
    NN_INPROC,
    nn_inproc_ctx_init,
    nn_inproc_ctx_term,
    nn_inproc_ctx_bind,
    nn_inproc_ctx_connect,
    NULL,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_inproc = &nn_inproc_vfptr;

struct nn_inproc_ctx {

    /*  Synchronises access to this object. */
    struct nn_mutex sync;

    /*  List of all bound inproc endpoints. */
    struct nn_list bound;

    /*  List of all connected inproc endpoints. */
    struct nn_list connected;

};

static struct nn_inproc_ctx self;

static void nn_inproc_ctx_init (void)
{
    nn_mutex_init (&self.sync);
    nn_list_init (&self.bound);
    nn_list_init (&self.connected);
}
static void nn_inproc_ctx_term (void)
{
    nn_list_term (&self.connected);
    nn_list_term (&self.bound);
    nn_mutex_term (&self.sync);
}

static int nn_inproc_ctx_bind (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_list_item *it;
    struct nn_inprocb *inprocb;
    struct nn_inprocc *inprocc;
    struct nn_msgpipe *pipe;

    nn_mutex_lock (&self.sync);

    /*  Check whether the endpoint isn't already bound. */
    /*  TODO:  This is an O(n) algorithm! */
    for (it = nn_list_begin (&self.bound); it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        inprocb = nn_cont (it, struct nn_inprocb, list);
        if (strncmp (addr, nn_inprocb_getaddr (inprocb),
              NN_SOCKADDR_MAX) == 0) {
            nn_mutex_unlock (&self.sync);
            return -EADDRINUSE;
        }
    }

    /*  Insert the entry into the endpoint repository. */
    inprocb = nn_alloc (sizeof (struct nn_inprocb), "inprocb");
    alloc_assert (inprocb);
    rc = nn_inprocb_init (inprocb, addr, hint);
    if (nn_slow (rc != 0)) {
        nn_mutex_unlock (&self.sync);
        return rc;
    }
    nn_list_insert (&self.bound, &inprocb->list, nn_list_end (&self.bound));

    /*  During this process new pipes may be created. */
    for (it = nn_list_begin (&self.connected);
          it != nn_list_end (&self.connected);
          it = nn_list_next (&self.connected, it)) {
        inprocc = nn_cont (it, struct nn_inprocc, list);
        if (strncmp (addr, nn_inprocc_getaddr (inprocc),
              NN_SOCKADDR_MAX) == 0) {
            if (!nn_inprocb_ispeer (inprocb, nn_inprocc_socktype (inprocc)) ||
                  !nn_inprocc_ispeer (inprocc, nn_inprocb_socktype (inprocb)))
                continue;
            pipe = nn_alloc (sizeof (struct nn_msgpipe), "msgpipe");
            alloc_assert (pipe);
            nn_msgpipe_init (pipe, inprocb, inprocc);
        }
    }

    nn_assert (epbase);
    *epbase = &inprocb->epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

static int nn_inproc_ctx_connect (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_list_item *it;
    struct nn_inprocc *inprocc;
    struct nn_inprocb *inprocb;
    struct nn_msgpipe *pipe;

    nn_mutex_lock (&self.sync);

    /*  Insert the entry into the endpoint repository. */
    inprocc = nn_alloc (sizeof (struct nn_inprocc), "inprocc");
    alloc_assert (inprocc);
    rc = nn_inprocc_init (inprocc, addr, hint);
    if (nn_slow (rc != 0)) {
        nn_mutex_unlock (&self.sync);
        return rc;
    }
    nn_list_insert (&self.connected, &inprocc->list,
        nn_list_end (&self.connected));

    /*  During this process a pipe may be created. */
    for (it = nn_list_begin (&self.bound);
          it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        inprocb = nn_cont (it, struct nn_inprocb, list);
        if (strcmp (addr, nn_inprocb_getaddr (inprocb)) == 0) {
            if (nn_inprocb_ispeer (inprocb, nn_inprocc_socktype (inprocc)) &&
                  nn_inprocc_ispeer (inprocc, nn_inprocb_socktype (inprocb))) {
                pipe = nn_alloc (sizeof (struct nn_msgpipe), "msgpipe");
                alloc_assert (pipe);
                nn_msgpipe_init (pipe, inprocb, inprocc);
            }
            break;
        }
    }

    nn_assert (epbase);
    *epbase = &inprocc->epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

void nn_inproc_ctx_unbind (struct nn_inprocb *inprocb)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.bound, &inprocb->list);
    nn_mutex_unlock (&self.sync);
}

void nn_inproc_ctx_disconnect (struct nn_inprocc *inprocc)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.connected, &inprocc->list);
    nn_mutex_unlock (&self.sync);
}

