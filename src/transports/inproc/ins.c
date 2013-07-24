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

#include "ins.h"

#include "binproc.h"
#include "cinproc.h"

#include "../../utils/mutex.h"
#include "../../utils/alloc.h"
#include "../../utils/list.h"
#include "../../utils/cont.h"
#include "../../utils/fast.h"
#include "../../utils/err.h"

struct nn_ins {

    /*  Synchronises access to this object. */
    struct nn_mutex sync;

    /*  List of all bound inproc endpoints. */
    /*  TODO: O(n) lookup, shouldn't we do better? Hash? */
    struct nn_list bound;

    /*  List of all connected inproc endpoints. */
    /*  TODO: O(n) lookup, shouldn't we do better? Hash? */
    struct nn_list connected;
};

/*  Global instance of the nn_ins object. It contains the lists of all
    inproc endpoints in the current process. */
static struct nn_ins self;

void nn_ins_item_init (struct nn_ins_item *self,
    const struct nn_epbase_vfptr *vfptr, void *hint)
{
    int protocol;
    size_t sz;

    nn_epbase_init (&self->epbase, vfptr, hint);
    nn_list_item_init (&self->item);
    sz = sizeof (self->protocol);
    nn_epbase_getopt (&self->epbase, NN_SOL_SOCKET, NN_PROTOCOL,
        &self->protocol, &sz);
    nn_assert (sz == sizeof (self->protocol));
    self->connects = 0;
}

void nn_ins_item_term (struct nn_ins_item *self)
{
    nn_list_item_term (&self->item);
    nn_epbase_term (&self->epbase);
}

void nn_ins_init (void)
{
    nn_mutex_init (&self.sync);
    nn_list_init (&self.bound);
    nn_list_init (&self.connected);
}
void nn_ins_term (void)
{
    nn_list_term (&self.connected);
    nn_list_term (&self.bound);
    nn_mutex_term (&self.sync);
}

int nn_ins_bind (const char *addr, void *hint, struct nn_epbase **epbase)
{
    struct nn_list_item *it;
    struct nn_binproc *binproc;
    struct nn_cinproc *cinproc;

    nn_mutex_lock (&self.sync);

    /*  Check whether the endpoint isn't already bound. */
    /*  TODO:  This is an O(n) algorithm! */
    for (it = nn_list_begin (&self.bound); it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        binproc = nn_cont (it, struct nn_binproc, item.item);
        if (strncmp (addr, nn_binproc_getaddr (binproc),
              NN_SOCKADDR_MAX) == 0) {
            nn_mutex_unlock (&self.sync);
            return -EADDRINUSE;
        }
    }

    /*  Insert the entry into the endpoint repository. */
    binproc = nn_binproc_create (hint);
    nn_list_insert (&self.bound, &binproc->item.item,
        nn_list_end (&self.bound));

    /*  During this process new pipes may be created. */
    for (it = nn_list_begin (&self.connected);
          it != nn_list_end (&self.connected);
          it = nn_list_next (&self.connected, it)) {
        cinproc = nn_cont (it, struct nn_cinproc, item.item);
        if (strncmp (addr, nn_cinproc_getaddr (cinproc),
              NN_SOCKADDR_MAX) == 0) {

            /*  Check whether the two sockets are compatible. */
            if (!nn_epbase_ispeer (&binproc->item.epbase,
                  cinproc->item.protocol))
                continue;

            nn_assert (cinproc->item.connects == 0);
            cinproc->item.connects = 1;
            nn_binproc_connect (binproc, cinproc);
        }
    }

    nn_assert (epbase);
    *epbase = &binproc->item.epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

int nn_ins_connect (const char *addr, void *hint, struct nn_epbase **epbase)
{
    struct nn_list_item *it;
    struct nn_cinproc *cinproc;
    struct nn_binproc *binproc;

    nn_mutex_lock (&self.sync);

    /*  Insert the entry into the endpoint repository. */
    cinproc = nn_cinproc_create (hint);
    nn_list_insert (&self.connected, &cinproc->item.item,
        nn_list_end (&self.connected));

    /*  During this process a pipe may be created. */
    for (it = nn_list_begin (&self.bound);
          it != nn_list_end (&self.bound);
          it = nn_list_next (&self.bound, it)) {
        binproc = nn_cont (it, struct nn_binproc, item.item);
        if (strncmp (addr, nn_binproc_getaddr (binproc),
              NN_SOCKADDR_MAX) == 0) {

            /*  Check whether the two sockets are compatible. */
            if (!nn_epbase_ispeer (&cinproc->item.epbase,
                  binproc->item.protocol))
                break;

            ++binproc->item.connects;
            nn_cinproc_connect (cinproc, binproc);
            break;
        }
    }

    nn_assert (epbase);
    *epbase = &cinproc->item.epbase;
    nn_mutex_unlock (&self.sync);

    return 0;
}

void nn_ins_disconnect (struct nn_ins_item *item)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.connected, &item->item);
    nn_mutex_unlock (&self.sync);
}

void nn_ins_unbind (struct nn_ins_item *item)
{
    nn_mutex_lock (&self.sync);
    nn_list_erase (&self.bound, &item->item);
    nn_mutex_unlock (&self.sync);
}

