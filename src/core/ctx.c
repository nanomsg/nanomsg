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

#include "../sp.h"
#include "../transport.h"
#include "../pattern.h"

#include "ctx.h"
#include "sock.h"
#include "ep.h"

#include "../utils/err.h"
#include "../utils/alloc.h"
#include "../utils/mutex.h"
#include "../utils/list.h"
#include "../utils/cont.h"
#include "../utils/cond.h"
#include "../utils/random.h"
#include "../utils/glock.h"

#include "../transports/inproc/inproc.h"
#include "../transports/tcp/tcp.h"

#include "../patterns/pair/pair.h"
#include "../patterns/pair/xpair.h"
#include "../patterns/pubsub/pub.h"
#include "../patterns/pubsub/sub.h"
#include "../patterns/reqrep/rep.h"
#include "../patterns/reqrep/req.h"
#include "../patterns/reqrep/xrep.h"
#include "../patterns/reqrep/xreq.h"
#include "../patterns/fanin/sink.h"
#include "../patterns/fanin/source.h"
#include "../patterns/fanin/xsink.h"
#include "../patterns/fanin/xsource.h"
#include "../patterns/fanout/push.h"
#include "../patterns/fanout/pull.h"
#include "../patterns/fanout/xpush.h"
#include "../patterns/fanout/xpull.h"
#include "../patterns/survey/respondent.h"
#include "../patterns/survey/surveyor.h"
#include "../patterns/survey/xrespondent.h"
#include "../patterns/survey/xsurveyor.h"

#include <stddef.h>

#if defined SP_HAVE_WINDOWS
#include "../utils/win.h"
#endif

/*  Max number of concurrent SP sockets. */
#define SP_MAX_SOCKETS 512

/*  Max number of concurrent SP endpoints. */
#define SP_MAX_EPS 512

/*  This check is performed at the beginning of each socket operation to make
    sure that the library was initialised and the socket actually exists. */
#define SP_BASIC_CHECKS \
    if (sp_slow (!self.socks)) {\
        errno = EFAULT;\
        return -1;\
    }\
    if (sp_slow (!self.socks [s])) {\
        errno = EBADF;\
        return -1;\
    }

struct sp_ctx {

    /*  Synchronisation of socket-related global state of the library.
        (following members). */
    struct sp_mutex ssync;

    /*  The global table of existing sockets. The descriptor representing
        the socket is the index to this table. */
    struct sp_sock **socks;
    size_t max_socks;

    /*  Number of actual open sockets in the socket table. */
    size_t nsocks;

    /*  1, if sp_term() was already called, 0 otherwise. */
    int zombie;

    /*  Synchronisation of endpoint-related global state of the library.
        (following members, the endpoints themselves, any global state in
        transports). */
    struct sp_mutex esync;

    /*  List of all existing endpoints including orphan endpoints. */
    struct sp_ep **eps;
    size_t max_eps;

    /*  List of all available transports. The access to this list is not
        synchronised. We assume that it never changes after the library was
        initialised. */
    struct sp_list transports;

    /*  List of all available socket types. */
    struct sp_list socktypes;

    /*  Next endpoint ID to use. */
    int next_eid;

    /*  Condition variable used by sp_term() to wait till all the sockets are
        closed. */
    struct sp_cond termcond;
};

/*  Number of times sp_init() was called without corresponding sp_term().
    This variable is synchronised using the global lock (sp_glock). */
int sp_ctx_refcount = 0;

/*  Singleton object containing the global state of the library. */
static struct sp_ctx self = {0};

/*  Transport-related private functions. */
static void sp_ctx_add_transport (struct sp_transport *transport);
static void sp_ctx_add_socktype (struct sp_socktype *socktype);
static struct sp_transport *sp_ctx_find_transport (const char *name,
    size_t len);

/*  Private function that unifies sp_bind and sp_connect functionality.
    It returns the ID of the newly created endpoint. */
static int sp_ctx_create_endpoint (int fd, const char *addr, int bind);

void sp_version (int *major, int *minor, int *patch)
{
    if (major)
        *major = SP_VERSION_MAJOR;
    if (minor)
        *minor = SP_VERSION_MINOR;
    if (patch)
        *patch = SP_VERSION_PATCH;
}

int sp_errno (void)
{
    return sp_err_errno ();
}

const char *sp_strerror (int errnum)
{
    return sp_err_strerror (errnum);
}

int sp_init (void)
{
    int i;
#if defined SP_HAVE_WINDOWS
    WSADATA data;
    int rc;
#endif

    sp_glock_lock ();

    /*  If the library is already initialised, do nothing, just increment
        the reference count. */
    ++sp_ctx_refcount;
    if (sp_ctx_refcount > 1) {
        sp_glock_unlock ();
        return 0;
    }

    /*  On Windows, initialise the socket library. */
#if defined SP_HAVE_WINDOWS
    rc = WSAStartup (MAKEWORD (2, 2), &data);
    sp_assert (rc == 0);
    sp_assert (LOBYTE (data.wVersion) == 2 &&
        HIBYTE (data.wVersion) == 2);
#endif

    /*  Initialise the memory allocation subsystem. */
    sp_alloc_init ();

    /*  Seed the pseudo-random number generator. */
    sp_random_seed ();

    /*  Allocate the global table of SP sockets. */
    self.max_socks = SP_MAX_SOCKETS;
    self.socks = sp_alloc (sizeof (struct sp_sock*) * self.max_socks,
        "socket table");
    alloc_assert (self.socks);
    for (i = 0; i != self.max_socks; ++i)
        self.socks [i] = NULL;
    self.nsocks = 0;
    self.zombie = 0;

    /*  Allocate the global table of SP endpoints. */
    self.max_eps = SP_MAX_EPS;
    self.eps = sp_alloc (sizeof (struct sp_ep*) * self.max_eps,
        "endpoint table");
    alloc_assert (self.eps);
    for (i = 0; i != self.max_eps; ++i)
        self.eps [i] = NULL;

    /*  Initialise other parts of the global state. */
    sp_mutex_init (&self.ssync);
    sp_mutex_init (&self.esync);
    sp_list_init (&self.transports);
    sp_list_init (&self.socktypes);
    self.next_eid = 1;
    sp_cond_init (&self.termcond);

    /*  Plug in individual transports. */
    sp_ctx_add_transport (sp_inproc);
    sp_ctx_add_transport (sp_tcp);

    /*  Plug in individual socktypes. */
    sp_ctx_add_socktype (sp_pair_socktype);
    sp_ctx_add_socktype (sp_xpair_socktype);
    sp_ctx_add_socktype (sp_pub_socktype);
    sp_ctx_add_socktype (sp_sub_socktype);
    sp_ctx_add_socktype (sp_rep_socktype);
    sp_ctx_add_socktype (sp_req_socktype);
    sp_ctx_add_socktype (sp_xrep_socktype);
    sp_ctx_add_socktype (sp_xreq_socktype);
    sp_ctx_add_socktype (sp_sink_socktype);
    sp_ctx_add_socktype (sp_source_socktype);
    sp_ctx_add_socktype (sp_xsink_socktype);
    sp_ctx_add_socktype (sp_xsource_socktype);
    sp_ctx_add_socktype (sp_push_socktype);
    sp_ctx_add_socktype (sp_pull_socktype);
    sp_ctx_add_socktype (sp_xpull_socktype);
    sp_ctx_add_socktype (sp_respondent_socktype);
    sp_ctx_add_socktype (sp_surveyor_socktype);
    sp_ctx_add_socktype (sp_xrespondent_socktype);
    sp_ctx_add_socktype (sp_xsurveyor_socktype);

    sp_glock_unlock ();

    return 0;
}

int sp_term (void)
{
#if defined SP_HAVE_WINDOWS
    int rc;
#endif
    int i;

    sp_glock_lock ();

    /*  If there are still references to the library, do nothing, just
        decrement the reference count. */
    --sp_ctx_refcount;
    if (sp_ctx_refcount) {
        sp_glock_unlock ();
        return 0;
    }

    /*  Notify all the open sockets about the process shutdown and wait till
        all of them are closed. */
    sp_mutex_lock (&self.ssync);
    if (self.nsocks) {
        for (i = 0; i != self.max_socks; ++i)
            if (self.socks [i])
                sp_sock_zombify (self.socks [i]);
        self.zombie = 1;
        sp_cond_wait (&self.termcond, &self.ssync, -1);
    }
    sp_mutex_unlock (&self.ssync);

    /*  TODO:  Wait for all endpoints to be closed. */

    /*  Final deallocation of the global resources. */
    sp_cond_term (&self.termcond);
    sp_list_term (&self.socktypes);
    sp_list_term (&self.transports);
    sp_mutex_term (&self.esync);
    sp_mutex_term (&self.ssync);
    sp_free (self.eps);
    self.eps = NULL;
    sp_free (self.socks);
    self.socks = NULL;

    /*  Shut down the memory allocation subsystem. */
    sp_alloc_term ();

    /*  On Windows, uninitialise the socket library. */
#if defined SP_HAVE_WINDOWS
    rc = WSACleanup ();
    sp_assert (rc == 0);
#endif

    sp_glock_unlock ();

    return 0;
}

int sp_socket (int domain, int protocol)
{
    int s;
    struct sp_list_item *it;
    struct sp_socktype *socktype;

    /*  Check whether library was initialised. */
    if (sp_slow (!self.socks)) {
        errno = EFAULT;
        return -1;
    }

    /*  Only AF_SP and AF_SP_RAW domains are supported. */
    if (sp_slow (domain != AF_SP && domain != AF_SP_RAW)) {
        errno = -EAFNOSUPPORT;
        return -1;
    }

    sp_mutex_lock (&self.ssync);

    /*  Find an empty socket slot. */
    /*  TODO: This is O(n) operation! Linked list of empty slots should be
        implemented. */
    for (s = 0; s != self.max_socks; ++s)
        if (!self.socks [s])
            break;

    /*  TODO: Auto-resize the array here! */
    if (sp_slow (s == self.max_socks)) {
        sp_mutex_unlock (&self.ssync);
        errno = EMFILE;
        return -1;
    }

    for (it = sp_list_begin (&self.socktypes);
          it != sp_list_end (&self.socktypes);
          it = sp_list_next (&self.socktypes, it)) {
        socktype = sp_cont (it, struct sp_socktype, list);
        if (socktype->domain == domain && socktype->protocol == protocol) {
            self.socks [s] = (struct sp_sock*) socktype->create (s);
            ++self.nsocks;
            sp_mutex_unlock (&self.ssync);
            return s;
        }
    }

    /*  Specified socket type wasn't found. */
    sp_mutex_unlock (&self.ssync);
    errno = EINVAL;
    return -1;
}

int sp_close (int s)
{
    int rc;
    int i;
    struct sp_ep *ep;

    SP_BASIC_CHECKS;

    sp_mutex_lock (&self.esync);

    /*  Ask all the endpoints associated with the socket to shut down. */
    /*  TODO:  This is O(n) algorithm. */
    for (i = 0; i != self.max_eps; ++i) {
        ep = self.eps [i];
        if (ep && sp_ep_fd (ep) == s) {
            rc = sp_ep_close (ep, 0); /*  TODO: linger */

            /*  TODO: The case where endpoint was already asked to shut down
                should be distinguished from the case where the shut down is
                processed asynchronously. */
            if (rc == -EINPROGRESS)
                continue;
            errnum_assert (rc == 0, -rc);
            self.eps [i] = NULL;
        }
    }

    sp_mutex_unlock (&self.esync);

    /*  Deallocate the socket object itself. */
    sp_mutex_lock (&self.ssync);
    sp_sock_term (self.socks [s]);
    sp_free (self.socks [s]);
    self.socks [s] = NULL;
    --self.nsocks;
    if (self.zombie && self.nsocks == 0)
        sp_cond_post (&self.termcond);
    sp_mutex_unlock (&self.ssync);

    return 0;
}

int sp_setsockopt (int s, int level, int option, const void *optval,
    size_t optvallen)
{
    int rc;

    SP_BASIC_CHECKS;

    if (sp_slow (!optval && optvallen)) {
        errno = EFAULT;
        return -1;
    }

    rc = sp_sock_setopt (self.socks [s], level, option, optval, optvallen);
    if (sp_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    errnum_assert (rc == 0, -rc);

    return 0;
}
 
int sp_getsockopt (int s, int level, int option, void *optval,
    size_t *optvallen)
{
    int rc;

    SP_BASIC_CHECKS;

    if (sp_slow (!optval && optvallen)) {
        errno = EFAULT;
        return -1;
    }

    rc = sp_sock_getopt (self.socks [s], level, option, optval, optvallen);
    if (sp_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    errnum_assert (rc == 0, -rc);

    return 0;
}

int sp_bind (int s, const char *addr)
{
    int rc;

    SP_BASIC_CHECKS;

    rc = sp_ctx_create_endpoint (s, addr, 1);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }

    return rc;
}

int sp_connect (int s, const char *addr)
{
    int rc;

    SP_BASIC_CHECKS;

    rc = sp_ctx_create_endpoint (s, addr, 0);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }

    return rc;
}

int sp_shutdown (int s, int how)
{
    int rc;
    struct sp_ep *ep;

    SP_BASIC_CHECKS;

    sp_mutex_lock (&self.esync);

    /*  Check whether the endpoint exists. */
    ep = self.eps [how];
    if (sp_slow (!ep)) {
        sp_mutex_unlock (&self.esync);
        errno = -EINVAL;
        return -1;
    }

    /*  Check whether the endpoint is associated with the socket in question. */
    if (sp_slow (sp_ep_fd (ep) != s)) {
        sp_mutex_unlock (&self.esync);
        errno = -EINVAL;
        return -1;
    }

    /*  Ask all the endpoint to shut down. */
    rc = sp_ep_close (ep, 0); /*  TODO: linger */

    /*  If the endpoint haven't deallocated itself immediately, do nothing. */
    /*  TODO: The case where endpoint was already asked to shut down should
        be distinguished from the case where the shut down is processed
        asynchronously. */
    if (rc == -EINPROGRESS) {
        sp_mutex_unlock (&self.esync);
        return 0;
    }
    errnum_assert (rc == 0, -rc);
    self.eps [how] = NULL;

    sp_mutex_unlock (&self.esync);

    return 0;
}

int sp_send (int s, const void *buf, size_t len, int flags)
{
    int rc;

    SP_BASIC_CHECKS;

    if (sp_slow (!buf && len)) {
        errno = EFAULT;
        return -1;
    }

    rc = sp_sock_send (self.socks [s], buf, len, flags);
    if (sp_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    sp_assert (rc == 0);

    return (int) len;
}

int sp_recv (int s, void *buf, size_t len, int flags)
{
    int rc;

    SP_BASIC_CHECKS;

    if (sp_slow (!buf && len)) {
        errno = EFAULT;
        return -1;
    }

    rc = sp_sock_recv (self.socks [s], buf, &len, flags);
    if (sp_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    sp_assert (rc == 0);

    return (int) len;
}

static void sp_ctx_add_transport (struct sp_transport *transport)
{
    transport->init ();
    sp_list_insert (&self.transports, &transport->list,
        sp_list_end (&self.transports));
}

static struct sp_transport *sp_ctx_find_transport (const char *name, size_t len)
{
    struct sp_list_item *it;
    struct sp_transport *tp;

    for (it = sp_list_begin (&self.transports);
          it != sp_list_end (&self.transports);
          it = sp_list_next (&self.transports, it)) {
        tp = sp_cont (it, struct sp_transport, list);
        if (strlen (tp->name ()) == len &&
              memcmp (tp->name (), name, len) == 0)
            return tp;
    }
    return NULL;
}

static void sp_ctx_add_socktype (struct sp_socktype *socktype)
{
    sp_list_insert (&self.socktypes, &socktype->list,
        sp_list_end (&self.socktypes));
}

static int sp_ctx_create_endpoint (int fd, const char *addr, int bind)
{
    int rc;
    int eid;
    const char *proto;
    const char *delim;
    size_t protosz;
    struct sp_transport *tp;
    struct sp_ep *ep;

    /*  Check whether address is valid. */
    if (!addr)
        return -EFAULT;
    if (strlen (addr) >= SP_SOCKADDR_MAX)
        return -EINVAL;

    /*  Separate the protocol and the actual address. */
    proto = addr;
    delim = strchr (addr, ':');
    if (!delim)
        return -EINVAL;
    if (delim [1] != '/' || delim [2] != '/')
        return -EINVAL;
    protosz = delim - addr;
    addr += protosz + 3;

    tp = sp_ctx_find_transport (proto, protosz);

    /*  The protocol specified doesn't match any known protocol. */
    if (!tp)
        return -EPROTONOSUPPORT;

    sp_mutex_lock (&self.esync);

    /*  Ask transport to create appropriate endpoint object. */
    if (bind)
        rc = tp->bind (addr, (void*) self.socks [fd],
            (struct sp_epbase**) &ep);
    else
        rc = tp->connect (addr, (void*) self.socks [fd],
            (struct sp_epbase**) &ep);
    if (sp_slow (rc != 0)) {
        sp_mutex_unlock (&self.esync);
        return rc;
    }
    sp_assert (ep);

    /*  Find an unused endpoint ID. */
    /*  TODO: This is O(n) operation! Linked list of empty endpoint IDs
        should be implemented. */
    for (eid = 0; eid != self.max_eps; ++eid)
        if (!self.eps [eid])
            break;

    /*  TODO: Auto-resize the array here! */
    if (sp_slow (eid == self.max_eps)) {
        sp_mutex_unlock (&self.esync);
        return -EMFILE;
    }

    /*  Store the reference to the endpoint. */
    self.eps [eid] = ep;

    sp_mutex_unlock (&self.esync);

    return eid;
}

