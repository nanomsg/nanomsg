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
#include "binproc.h"
#include "cinproc.h"

#include "../../inproc.h"

#include "../../utils/mutex.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

#include <string.h>

/*  nn_transport interface. */
static void nn_inproc_init (void);
static void nn_inproc_term (void);
static int nn_inproc_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_inproc_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);

static struct nn_transport nn_inproc_vfptr = {
    "inproc",
    NN_INPROC,
    nn_inproc_init,
    nn_inproc_term,
    nn_inproc_bind,
    nn_inproc_connect,
    NULL,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_inproc = &nn_inproc_vfptr;

struct nn_inproc {

    /*  Synchronises access to this object. */
    struct nn_mutex sync;

    /*  List of all bound inproc endpoints. */
    /*  TODO: O(n) lookup, shouldn't we do better? Hash? */
    struct nn_list bound;

    /*  List of all connected inproc endpoints. */
    /*  TODO: O(n) lookup, shouldn't we do better? Hash? */
    struct nn_list connected;
};

/*  Global instance of the nn_inproc object. It contains the lists of all
    inproc endpoints in the current process. */
static struct nn_inproc self;

static void nn_inproc_init (void)
{
    nn_mutex_init (&self.sync);
    nn_list_init (&self.bound);
    nn_list_init (&self.connected);
}
static void nn_inproc_term (void)
{
    nn_list_term (&self.connected);
    nn_list_term (&self.bound);
    nn_mutex_term (&self.sync);
}

static int nn_inproc_bind (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    struct nn_list_item *it;
    struct nn_binproc *binproc;
    struct nn_cinproc *cinproc;
    struct nn_msgpipe *pipe;

    nn_mutex_lock (&self.sync);

    /*  Check whether the endpoint isn't already bound. */
    /*  TODO:  This is an O(n) algorithm! */
    for (it = nn_list_begin (&self.bound); it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        binproc = nn_cont (it, struct nn_binproc, item);
        if (strncmp (addr, nn_binproc_getaddr (binproc),
              NN_SOCKADDR_MAX) == 0) {
            nn_mutex_unlock (&self.sync);
            return -EADDRINUSE;
        }
    }

    /*  Insert the entry into the endpoint repository. */
    binproc = nn_binproc_create (hint);
    nn_list_insert (&self.bound, &binproc->item, nn_list_end (&self.bound));

    /*  During this process new pipes may be created. */
    for (it = nn_list_begin (&self.connected);
          it != nn_list_end (&self.connected);
          it = nn_list_next (&self.connected, it)) {
        cinproc = nn_cont (it, struct nn_cinproc, item);
        if (strncmp (addr, nn_cinproc_getaddr (cinproc),
              NN_SOCKADDR_MAX) == 0) {
#if 0
            if (!nn_binproc_ispeer (binproc, nn_cinproc_socktype (cinproc)) ||
                  !nn_cinproc_ispeer (cinproc, nn_binproc_socktype (binproc)))
                continue;
#endif
            /*  TODO: Create the pipe here. */
            nn_assert (0);
        }
    }

    nn_assert (epbase);
    *epbase = &binproc->epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

static int nn_inproc_connect (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    struct nn_list_item *it;
    struct nn_cinproc *cinproc;
    struct nn_binproc *binproc;
    struct nn_msgpipe *pipe;

    nn_mutex_lock (&self.sync);

    /*  Insert the entry into the endpoint repository. */
    cinproc = nn_cinproc_create (hint);
    nn_list_insert (&self.connected, &cinproc->item,
        nn_list_end (&self.connected));

    /*  During this process a pipe may be created. */
    for (it = nn_list_begin (&self.bound);
          it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        binproc = nn_cont (it, struct nn_binproc, item);
        if (strcmp (addr, nn_binproc_getaddr (binproc)) == 0) {
#if 0
            if (nn_binproc_ispeer (binproc, nn_cinproc_socktype (cinproc)) &&
                  nn_cinproc_ispeer (cinproc, nn_binproc_socktype (binproc))) {
#endif
                /*  TODO: Create the pipe here. */
                nn_assert (0);
#if 0
            }
#endif
            break;
        }
    }

    nn_assert (epbase);
    *epbase = &cinproc->epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

void nn_inproc_unbind (struct nn_binproc *binproc)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.bound, &binproc->item);
    nn_mutex_unlock (&self.sync);
}

void nn_inproc_disconnect (struct nn_cinproc *cinproc)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.connected, &cinproc->item);
    nn_mutex_unlock (&self.sync);
}

