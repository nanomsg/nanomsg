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

#include "../nn.h"
#include "../transport.h"
#include "../protocol.h"

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
#include "../utils/latmon.h"
#include "../utils/glock.h"
#include "../utils/chunk.h"
#include "../utils/msg.h"

#include "../transports/inproc/inproc.h"
#include "../transports/ipc/ipc.h"
#include "../transports/tcp/tcp.h"

#include "../protocols/pair/pair.h"
#include "../protocols/pair/xpair.h"
#include "../protocols/pubsub/pub.h"
#include "../protocols/pubsub/sub.h"
#include "../protocols/reqrep/rep.h"
#include "../protocols/reqrep/req.h"
#include "../protocols/reqrep/xrep.h"
#include "../protocols/reqrep/xreq.h"
#include "../protocols/fanin/sink.h"
#include "../protocols/fanin/source.h"
#include "../protocols/fanin/xsink.h"
#include "../protocols/fanin/xsource.h"
#include "../protocols/fanout/push.h"
#include "../protocols/fanout/pull.h"
#include "../protocols/fanout/xpush.h"
#include "../protocols/fanout/xpull.h"
#include "../protocols/survey/respondent.h"
#include "../protocols/survey/surveyor.h"
#include "../protocols/survey/xrespondent.h"
#include "../protocols/survey/xsurveyor.h"

#include <stddef.h>
#include <string.h>

#if defined NN_HAVE_WINDOWS
#include "../utils/win.h"
#endif

/*  Max number of concurrent SP sockets. */
#define NN_MAX_SOCKETS 512
CT_ASSERT (NN_MAX_SOCKETS <= 0xffff);

/*  This check is performed at the beginning of each socket operation to make
    sure that the library was initialised and the socket actually exists. */
#define NN_BASIC_CHECKS \
    if (nn_slow (!self.socks)) {\
        errno = EFAULT;\
        return -1;\
    }\
    if (nn_slow (!self.socks [s])) {\
        errno = EBADF;\
        return -1;\
    }

struct nn_ctx {

    /*  Synchronisation of global state of the library. */
    struct nn_mutex sync;

    /*  The global table of existing sockets. The descriptor representing
        the socket is the index to this table. */
    struct nn_sock **socks;

    /*  Stack of unused file descriptors. */
    uint16_t *unused;

    /*  Number of actual open sockets in the socket table. */
    size_t nsocks;

    /*  1, if nn_term() was already called, 0 otherwise. */
    int zombie;

    /*  List of all available transports. The access to this list is not
        synchronised. We assume that it never changes after the library was
        initialised. */
    struct nn_list transports;

    /*  List of all available socket types. */
    struct nn_list socktypes;

    /*  Condition variable used by nn_term() to wait till all the sockets are
        closed. */
    struct nn_cond termcond;
};

/*  Number of times nn_init() was called without corresponding nn_term().
    This variable is synchronised using the global lock (nn_glock). */
int nn_ctx_refcount = 0;

/*  Singleton object containing the global state of the library. */
static struct nn_ctx self = {0};

/*  Transport-related private functions. */
static void nn_ctx_add_transport (struct nn_transport *transport);
static void nn_ctx_add_socktype (struct nn_socktype *socktype);

/*  Private function that unifies nn_bind and nn_connect functionality.
    It returns the ID of the newly created endpoint. */
static int nn_ctx_create_ep (int fd, const char *addr, int bind);

void nn_version (int *major, int *minor, int *patch)
{
    if (major)
        *major = NN_VERSION_MAJOR;
    if (minor)
        *minor = NN_VERSION_MINOR;
    if (patch)
        *patch = NN_VERSION_PATCH;
}

int nn_errno (void)
{
    return nn_err_errno ();
}

const char *nn_strerror (int errnum)
{
    return nn_err_strerror (errnum);
}

struct nn_cmsghdr *nn_cmsg_nexthdr (const struct nn_msghdr *mhdr,
    const struct nn_cmsghdr *cmsg)
{
    size_t sz;

    sz = sizeof (struct nn_cmsghdr) + cmsg->cmsg_len;
    if (((uint8_t*) cmsg) - ((uint8_t*) mhdr->msg_control) + sz >=
           mhdr->msg_controllen)
        return NULL;
    return (struct nn_cmsghdr*) (((uint8_t*) cmsg) + sz);
}

int nn_init (void)
{
    int i;
#if defined NN_HAVE_WINDOWS
    WSADATA data;
    int rc;
#endif

    nn_glock_lock ();

    /*  If the library is already initialised, do nothing, just increment
        the reference count. */
    ++nn_ctx_refcount;
    if (nn_ctx_refcount > 1) {
        nn_glock_unlock ();
        return 0;
    }

    /*  On Windows, initialise the socket library. */
#if defined NN_HAVE_WINDOWS
    rc = WSAStartup (MAKEWORD (2, 2), &data);
    nn_assert (rc == 0);
    nn_assert (LOBYTE (data.wVersion) == 2 &&
        HIBYTE (data.wVersion) == 2);
#endif

    /*  Initialise the memory allocation subsystem. */
    nn_alloc_init ();

    /*  Seed the pseudo-random number generator. */
    nn_random_seed ();

    /*  Allocate the global table of SP sockets. */
    self.socks = nn_alloc (sizeof (struct nn_sock*) * NN_MAX_SOCKETS,
        "socket table");
    alloc_assert (self.socks);
    for (i = 0; i != NN_MAX_SOCKETS; ++i)
        self.socks [i] = NULL;
    self.nsocks = 0;
    self.zombie = 0;

    /*  Allocate the stack of unused file descriptors. */
    self.unused = nn_alloc (sizeof (uint16_t) * NN_MAX_SOCKETS,
        "unused socket table");
    alloc_assert (self.unused);
    for (i = 0; i != NN_MAX_SOCKETS; ++i)
        self.unused [i] = NN_MAX_SOCKETS - i - 1;

    /*  Initialise other parts of the global state. */
    nn_mutex_init (&self.sync);
    nn_list_init (&self.transports);
    nn_list_init (&self.socktypes);
    nn_cond_init (&self.termcond);

    /*  Plug in individual transports. */
    nn_ctx_add_transport (nn_inproc);
#if !defined NN_HAVE_WINDOWS
    nn_ctx_add_transport (nn_ipc);
#endif
    nn_ctx_add_transport (nn_tcp);

    /*  Plug in individual socktypes. */
    nn_ctx_add_socktype (nn_pair_socktype);
    nn_ctx_add_socktype (nn_xpair_socktype);
    nn_ctx_add_socktype (nn_pub_socktype);
    nn_ctx_add_socktype (nn_sub_socktype);
    nn_ctx_add_socktype (nn_rep_socktype);
    nn_ctx_add_socktype (nn_req_socktype);
    nn_ctx_add_socktype (nn_xrep_socktype);
    nn_ctx_add_socktype (nn_xreq_socktype);
    nn_ctx_add_socktype (nn_sink_socktype);
    nn_ctx_add_socktype (nn_source_socktype);
    nn_ctx_add_socktype (nn_xsink_socktype);
    nn_ctx_add_socktype (nn_xsource_socktype);
    nn_ctx_add_socktype (nn_push_socktype);
    nn_ctx_add_socktype (nn_pull_socktype);
    nn_ctx_add_socktype (nn_xpull_socktype);
    nn_ctx_add_socktype (nn_respondent_socktype);
    nn_ctx_add_socktype (nn_surveyor_socktype);
    nn_ctx_add_socktype (nn_xrespondent_socktype);
    nn_ctx_add_socktype (nn_xsurveyor_socktype);

#if defined NN_LATENCY_MONITOR
    nn_latmon_init ();
#endif

    nn_glock_unlock ();

    return 0;
}

int nn_term (void)
{
#if defined NN_HAVE_WINDOWS
    int rc;
#endif
    int i;

    nn_glock_lock ();

    /*  If there are still references to the library, do nothing, just
        decrement the reference count. */
    --nn_ctx_refcount;
    if (nn_ctx_refcount) {
        nn_glock_unlock ();
        return 0;
    }

    /*  Notify all the open sockets about the process shutdown and wait till
        all of them are closed. */
    nn_mutex_lock (&self.sync);
    if (self.nsocks) {
        for (i = 0; i != NN_MAX_SOCKETS; ++i)
            if (self.socks [i])
                nn_sock_zombify (self.socks [i]);
        self.zombie = 1;
        nn_cond_wait (&self.termcond, &self.sync);
    }
    nn_mutex_unlock (&self.sync);

#if defined NN_LATENCY_MONITOR
    nn_latmon_term ();
#endif

    /*  Final deallocation of the global resources. */
    nn_cond_term (&self.termcond);
    nn_list_term (&self.socktypes);
    nn_list_term (&self.transports);
    nn_mutex_term (&self.sync);
    nn_free (self.unused);
    nn_free (self.socks);
    self.socks = NULL;

    /*  Shut down the memory allocation subsystem. */
    nn_alloc_term ();

    /*  On Windows, uninitialise the socket library. */
#if defined NN_HAVE_WINDOWS
    rc = WSACleanup ();
    nn_assert (rc == 0);
#endif

    nn_glock_unlock ();

    return 0;
}

void *nn_allocmsg (size_t size, int type)
{
    struct nn_chunk *ch;

    ch = nn_chunk_alloc (size, type);
    if (nn_slow (!ch))
        return NULL;
    return (void*) (ch + 1);
}

int nn_freemsg (void *msg)
{
    nn_chunk_free (((struct nn_chunk*) msg) - 1);
    return 0;
}

int nn_socket (int domain, int protocol)
{
    int s;
    struct nn_list_item *it;
    struct nn_socktype *socktype;

    /*  Check whether library was initialised. */
    if (nn_slow (!self.socks)) {
        errno = EFAULT;
        return -1;
    }

    /*  Only AF_SP and AF_SP_RAW domains are supported. */
    if (nn_slow (domain != AF_SP && domain != AF_SP_RAW)) {
        errno = -EAFNOSUPPORT;
        return -1;
    }

    nn_mutex_lock (&self.sync);

    /*  If socket limit was reached, report error. */
    if (nn_slow (self.nsocks >= NN_MAX_SOCKETS)) {
        nn_mutex_unlock (&self.sync);
        errno = EMFILE;
        return -1;
    }

    /*  Find an empty socket slot. */
    s = self.unused [NN_MAX_SOCKETS - self.nsocks - 1];

    /*  Find the appropriate socket type and instantiate it. */
    for (it = nn_list_begin (&self.socktypes);
          it != nn_list_end (&self.socktypes);
          it = nn_list_next (&self.socktypes, it)) {
        socktype = nn_cont (it, struct nn_socktype, list);
        if (socktype->domain == domain && socktype->protocol == protocol) {
            self.socks [s] = (struct nn_sock*) socktype->create (s);
            ++self.nsocks;
            nn_mutex_unlock (&self.sync);
            return s;
        }
    }

    /*  Specified socket type wasn't found. */
    nn_mutex_unlock (&self.sync);
    errno = EINVAL;
    return -1;
}

int nn_close (int s)
{
    NN_BASIC_CHECKS;

    /*  Additional check of socket validity. */
    nn_assert (self.nsocks > 0);

    /*  Deallocate the socket object. */
    nn_sock_term (self.socks [s]);

    nn_mutex_lock (&self.sync);

    /*  Remove the socket from the socket table, add it to unused socket
        table. */
    self.socks [s] = NULL;
    self.unused [NN_MAX_SOCKETS - self.nsocks] = s;
    --self.nsocks;

    /*  If there's nn_term() waiting for all sockets being closed and this is
        the last open socket let library termination proceed. */
    if (self.zombie && self.nsocks == 0)
        nn_cond_post (&self.termcond);

    nn_mutex_unlock (&self.sync);

    return 0;
}

int nn_setsockopt (int s, int level, int option, const void *optval,
    size_t optvallen)
{
    int rc;

    NN_BASIC_CHECKS;

    if (nn_slow (!optval && optvallen)) {
        errno = EFAULT;
        return -1;
    }

    rc = nn_sock_setopt (self.socks [s], level, option, optval, optvallen);
    if (nn_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    errnum_assert (rc == 0, -rc);

    return 0;
}
 
int nn_getsockopt (int s, int level, int option, void *optval,
    size_t *optvallen)
{
    int rc;

    NN_BASIC_CHECKS;

    if (nn_slow (!optval && optvallen)) {
        errno = EFAULT;
        return -1;
    }

    rc = nn_sock_getopt (self.socks [s], level, option, optval, optvallen, 0);
    if (nn_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    errnum_assert (rc == 0, -rc);

    return 0;
}

int nn_bind (int s, const char *addr)
{
    int rc;

    NN_BASIC_CHECKS;

    rc = nn_ctx_create_ep (s, addr, 1);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }

    return rc;
}

int nn_connect (int s, const char *addr)
{
    int rc;

    NN_BASIC_CHECKS;

    rc = nn_ctx_create_ep (s, addr, 0);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }

    return rc;
}

int nn_shutdown (int s, int how)
{
    int rc;

    NN_BASIC_CHECKS;

    rc = nn_sock_shutdown (self.socks [s], how);
    if (nn_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }
    nn_assert (rc == 0);

    return 0;
}

int nn_send (int s, const void *buf, size_t len, int flags)
{
    int rc;
    struct nn_msg msg;
    struct nn_chunk *ch;

    NN_BASIC_CHECKS;

#if defined NN_LATENCY_MONITOR
    nn_latmon_measure (NN_LATMON_SEND);
#endif

    if (nn_slow (!buf && len)) {
        errno = EFAULT;
        return -1;
    }

    /*  Create a message object. */
    if (len == NN_MSG) {
        ch = (*(struct nn_chunk**) buf) - 1;
        rc = nn_chunk_check (ch);
        if (nn_slow (rc < 0)) {
            errno = -rc;
            return -1;
        }
        len = nn_chunk_size (ch);
        nn_msg_init_chunk (&msg, ch);
    }
    else {
        nn_msg_init (&msg, len);
        memcpy (nn_chunkref_data (&msg.body), buf, len);
    }

    /*  Send it further down the stack. */
    rc = nn_sock_send (self.socks [s], &msg, flags);
    if (nn_slow (rc < 0)) {
        nn_msg_term (&msg);
        errno = -rc;
        return -1;
    }

    return (int) len;
}

int nn_recv (int s, void *buf, size_t len, int flags)
{
    int rc;
    struct nn_msg msg;
    size_t sz;
    struct nn_chunk *ch;

    NN_BASIC_CHECKS;

    if (nn_slow (!buf && len)) {
        errno = EFAULT;
        return -1;
    }

    rc = nn_sock_recv (self.socks [s], &msg, flags);
    if (nn_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }

    if (len == NN_MSG) {
        ch = nn_chunkref_getchunk (&msg.body);
        *(void**) buf = (void*) (ch + 1);
        sz = nn_chunk_size (ch);
    }
    else {
        sz = nn_chunkref_size (&msg.body);
        memcpy (buf, nn_chunkref_data (&msg.body), len < sz ? len : sz);
    }
    nn_msg_term (&msg);

#if defined NN_LATENCY_MONITOR
    nn_latmon_measure (NN_LATMON_RECV);
#endif

    return (int) sz;
}

int nn_sendmsg (int s, const struct nn_msghdr *msghdr, int flags)
{
    int rc;
    size_t sz;
    int i;
    struct nn_iovec *iov;
    struct nn_msg msg;
    struct nn_chunk *ch;

    NN_BASIC_CHECKS;

#if defined NN_LATENCY_MONITOR
    nn_latmon_measure (NN_LATMON_SEND);
#endif

    if (nn_slow (!msghdr)) {
        errno = EINVAL;
        return -1;
    }

    if (nn_slow (msghdr->msg_iovlen < 0)) {
        errno = EMSGSIZE;
        return -1;
    }

    if (msghdr->msg_iovlen == 1 && msghdr->msg_iov [0].iov_len == NN_MSG) {
        ch = (*(struct nn_chunk**) msghdr->msg_iov [0].iov_base) - 1;
        sz = nn_chunk_size (ch);
        nn_msg_init_chunk (&msg, ch);
    }
    else {

        /*  Compute the total size of the message. */
        sz = 0;
        for (i = 0; i != msghdr->msg_iovlen; ++i) {
            iov = &msghdr->msg_iov [i];
            if (nn_slow (iov->iov_len == NN_MSG)) {
               errno = EINVAL;
               return -1;
            }
            if (nn_slow (!iov->iov_base && iov->iov_len)) {
                errno = EFAULT;
                return -1;
            }
            if (nn_slow (sz + iov->iov_len < sz)) {
                errno = EINVAL;
                return -1;
            }
            sz += iov->iov_len;
        }

        /*  Create a message object from the supplied scatter array. */
        nn_msg_init (&msg, sz);
        sz = 0;
        for (i = 0; i != msghdr->msg_iovlen; ++i) {
            iov = &msghdr->msg_iov [i];
            memcpy (((uint8_t*) nn_chunkref_data (&msg.body)) + sz,
                iov->iov_base, iov->iov_len);
            sz += iov->iov_len;
        }
    }

    /*  Add ancillary data to the message. */
    if (msghdr->msg_control) {
        rc = nn_sock_sethdr (self.socks [s], &msg,
            msghdr->msg_control, msghdr->msg_controllen);
        if (nn_slow (rc < 0)) {
            nn_msg_term (&msg);
            errno = -rc;
            return -1;
        }
    }

    /*  Send it further down the stack. */
    rc = nn_sock_send (self.socks [s], &msg, flags);
    if (nn_slow (rc < 0)) {
        nn_msg_term (&msg);
        errno = -rc;
        return -1;
    }

    return (int) sz;
}

int nn_recvmsg (int s, struct nn_msghdr *msghdr, int flags)
{
    int rc;
    struct nn_msg msg;
    uint8_t *data;
    size_t sz;
    int i;
    struct nn_iovec *iov;
    struct nn_chunk *ch;

    NN_BASIC_CHECKS;

    if (nn_slow (!msghdr)) {
        errno = EINVAL;
        return -1;
    }

    if (nn_slow (msghdr->msg_iovlen < 0)) {
        errno = EMSGSIZE;
        return -1;
    }

    /*  Get a message. */
    rc = nn_sock_recv (self.socks [s], &msg, flags);
    if (nn_slow (rc < 0)) {
        errno = -rc;
        return -1;
    }

    if (msghdr->msg_iovlen == 1 && msghdr->msg_iov [0].iov_len == NN_MSG) {
        ch = nn_chunkref_getchunk (&msg.body);
        *(void**) (msghdr->msg_iov [0].iov_base) = (void*) (ch + 1);
        sz = nn_chunk_size (ch);
    }
    else {

        /*  Copy the message content into the supplied gather array. */
        data = nn_chunkref_data (&msg.body);
        sz = nn_chunkref_size (&msg.body);
        for (i = 0; i != msghdr->msg_iovlen; ++i) {
            iov = &msghdr->msg_iov [i];
            if (nn_slow (iov->iov_len == NN_MSG)) {
                nn_msg_term (&msg);
                errno = EINVAL;
                return -1;
            }
            if (iov->iov_len > sz) {
                memcpy (iov->iov_base, data, sz);
                break;
            }
            memcpy (iov->iov_base, data, iov->iov_len);
            data += iov->iov_len;
            sz -= iov->iov_len;
        }
        sz = nn_chunkref_size (&msg.body);
    }

    /*  Retrieve the ancillary data from the message. */
    if (msghdr->msg_control) {
        rc = nn_sock_gethdr (self.socks [s], &msg,
            msghdr->msg_control, &msghdr->msg_controllen);
        if (nn_slow (rc < 0)) {
            nn_msg_term (&msg);
            errno = -rc;
            return -1;
        }
    }

    nn_msg_term (&msg);

#if defined NN_LATENCY_MONITOR
    nn_latmon_measure (NN_LATMON_RECV);
#endif

    return (int) sz;
}

static void nn_ctx_add_transport (struct nn_transport *transport)
{
    transport->init ();
    nn_list_insert (&self.transports, &transport->list,
        nn_list_end (&self.transports));
}

static void nn_ctx_add_socktype (struct nn_socktype *socktype)
{
    nn_list_insert (&self.socktypes, &socktype->list,
        nn_list_end (&self.socktypes));
}

static int nn_ctx_create_ep (int fd, const char *addr, int bind)
{
    const char *proto;
    const char *delim;
    size_t protosz;
    struct nn_transport *tp;
    struct nn_list_item *it;

    /*  Check whether address is valid. */
    if (!addr)
        return -EINVAL;
    if (strlen (addr) >= NN_SOCKADDR_MAX)
        return -ENAMETOOLONG;

    /*  Separate the protocol and the actual address. */
    proto = addr;
    delim = strchr (addr, ':');
    if (!delim)
        return -EINVAL;
    if (delim [1] != '/' || delim [2] != '/')
        return -EINVAL;
    protosz = delim - addr;
    addr += protosz + 3;

    /*  Find the specified protocol. */
    tp = NULL;
    nn_mutex_lock (&self.sync);
    for (it = nn_list_begin (&self.transports);
          it != nn_list_end (&self.transports);
          it = nn_list_next (&self.transports, it)) {
        tp = nn_cont (it, struct nn_transport, list);
        if (strlen (tp->name ()) == protosz &&
              memcmp (tp->name (), proto, protosz) == 0)
            break;
        tp = NULL;
    }
    nn_mutex_unlock (&self.sync);

    /*  The protocol specified doesn't match any known protocol. */
    if (!tp)
        return -EPROTONOSUPPORT;

    /*  Ask socket to create the endpoint. Pass it the class factory
        function. */
    return nn_sock_create_ep (self.socks [fd], addr,
        bind ? tp->bind : tp->connect);
}

