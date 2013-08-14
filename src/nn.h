/*
    Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#ifndef NN_H_INCLUDED
#define NN_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <stddef.h>

/*  Handle DSO symbol visibility                                             */
#if defined _WIN32
#   if defined NN_EXPORTS
#       define NN_EXPORT __declspec(dllexport)
#   else
#       define NN_EXPORT __declspec(dllimport)
#   endif
#else
#   if defined __SUNPRO_C
#       define NN_EXPORT __global
#   elif (defined __GNUC__ && __GNUC__ >= 4) || \
          defined __INTEL_COMPILER || defined __clang__
#       define NN_EXPORT __attribute__ ((visibility("default")))
#   else
#       define NN_EXPORT
#   endif
#endif

/******************************************************************************/
/*  ABI versioning support.                                                   */
/******************************************************************************/

/*  Don't change this unless you know exactly what you're doing and have      */
/*  read and understand the following documents:                              */
/*  www.gnu.org/software/libtool/manual/html_node/Libtool-versioning.html     */
/*  www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html  */

/*  The current interface version. */
#define NN_VERSION_CURRENT 0

/*  The latest revision of the current interface. */
#define NN_VERSION_REVISION 0

/*  How many past interface versions are still supported. */
#define NN_VERSION_AGE 0

/******************************************************************************/
/*  Errors.                                                                   */
/******************************************************************************/

/*  A number random enough not to collide with different errno ranges on      */
/*  different OSes. The assumption is that error_t is at least 32-bit type.   */
#define NN_HAUSNUMERO 156384712

/*  On some platforms some standard POSIX errnos are not defined.    */
#ifndef ENOTSUP
#define ENOTSUP (NN_HAUSNUMERO + 1)
#define NN_ENOTSUP_DEFINED
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (NN_HAUSNUMERO + 2)
#define NN_EPROTONOSUPPORT_DEFINED
#endif
#ifndef ENOBUFS
#define ENOBUFS (NN_HAUSNUMERO + 3)
#define NN_ENOBUFS_DEFINED
#endif
#ifndef ENETDOWN
#define ENETDOWN (NN_HAUSNUMERO + 4)
#define NN_ENETDOWN_DEFINED
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (NN_HAUSNUMERO + 5)
#define NN_EADDRINUSE_DEFINED
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (NN_HAUSNUMERO + 6)
#define NN_EADDRNOTAVAIL_DEFINED
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (NN_HAUSNUMERO + 7)
#define NN_ECONNREFUSED_DEFINED
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (NN_HAUSNUMERO + 8)
#define NN_EINPROGRESS_DEFINED
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (NN_HAUSNUMERO + 9)
#define NN_ENOTSOCK_DEFINED
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (NN_HAUSNUMERO + 10)
#define NN_EAFNOSUPPORT_DEFINED
#endif
#ifndef EPROTO
#define EPROTO (NN_HAUSNUMERO + 11)
#define NN_EPROTO_DEFINED
#endif

/*  Native error codes.                                                       */
#ifndef ETERM
#define ETERM (NN_HAUSNUMERO + 53)
#endif
#ifndef EFSM
#define EFSM (NN_HAUSNUMERO + 54)
#endif

/*  This function retrieves the errno as it is known to the library.          */
/*  The goal of this function is to make the code 100% portable, including    */
/*  where the library is compiled with certain CRT library (on Windows) and   */
/*  linked to an application that uses different CRT library.                 */
NN_EXPORT int nn_errno (void);

/*  Resolves system errors and native errors to human-readable string.        */
NN_EXPORT const char *nn_strerror (int errnum);

/*  Returns the symbol name (e.g. "NN_REQ") and value at a specified index.   */
/*  If the index is out-of-range, returns NULL and sets errno to EINVAL       */
/*  General usage is to start at i=0 and iterate until NULL is returned.      */
NN_EXPORT const char *nn_symbol (int i, int *value);

/******************************************************************************/
/*  Helper function for shutting down multi-threaded applications.            */
/******************************************************************************/

NN_EXPORT void nn_term (void);

/******************************************************************************/
/*  Zero-copy support.                                                        */
/******************************************************************************/

#define NN_MSG ((size_t) -1)

NN_EXPORT void *nn_allocmsg (size_t size, int type);
NN_EXPORT int nn_freemsg (void *msg);

/******************************************************************************/
/*  Socket definition.                                                        */
/******************************************************************************/

struct nn_iovec {
    void *iov_base;
    size_t iov_len;
};

struct nn_msghdr {
    struct nn_iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
};

struct nn_cmsghdr {
    size_t cmsg_len;
    int cmsg_level;
    int cmsg_type;
};

/*  Internal function. Not to be used directly.                               */
/*  Use NN_CMSG_NEXTHDR macro instead.                                        */
NN_EXPORT struct nn_cmsghdr *nn_cmsg_nexthdr (const struct nn_msghdr *mhdr,
    const struct nn_cmsghdr *cmsg);

#define NN_CMSG_FIRSTHDR(mhdr) \
    ((mhdr)->msg_controllen >= sizeof (struct nn_cmsghdr) \
    ? (struct nn_cmsghdr*) (mhdr)->msg_control : (struct nn_cmsghdr*) NULL)

#define NN_CMSG_NXTHDR(mhdr,cmsg) \
    nn_cmsg_nexthdr ((struct nn_msghdr*) (mhdr), (struct nn_cmsghdr*) (cmsg))

#define NN_CMSG_DATA(cmsg) \
    ((unsigned char*) (((struct nn_cmsghdr*) (cmsg)) + 1))

/*  Helper macro. Not to be used directly.                                    */
#define NN_CMSG_ALIGN(len) \
    (((len) + sizeof (size_t) - 1) & (size_t) ~(sizeof (size_t) - 1))

/* Extensions to POSIX defined by RFC3542.                                    */

#define NN_CMSG_SPACE(len) \
    (CMSG_ALIGN (len) + CMSG_ALIGN (sizeof (struct nn_cmsghdr)))

#define NN_CMSG_LEN(len) \
    (CMSG_ALIGN (sizeof (struct nn_cmsghdr)) + (len))

/*  SP address families.                                                      */
#define AF_SP 1
#define AF_SP_RAW 2

/*  Max size of an SP address.                                                */
#define NN_SOCKADDR_MAX 128

/*  Socket option levels: Negative numbers are reserved for transports,
    positive for socket types. */
#define NN_SOL_SOCKET 0

/*  Generic socket options (NN_SOL_SOCKET level).                             */
#define NN_LINGER 1
#define NN_SNDBUF 2
#define NN_RCVBUF 3
#define NN_SNDTIMEO 4
#define NN_RCVTIMEO 5
#define NN_RECONNECT_IVL 6
#define NN_RECONNECT_IVL_MAX 7
#define NN_SNDPRIO 8
#define NN_SNDFD 10
#define NN_RCVFD 11
#define NN_DOMAIN 12
#define NN_PROTOCOL 13
#define NN_IPV4ONLY 14

/*  Send/recv options.                                                        */
#define NN_DONTWAIT 1

NN_EXPORT int nn_socket (int domain, int protocol);
NN_EXPORT int nn_close (int s);
NN_EXPORT int nn_setsockopt (int s, int level, int option, const void *optval,
    size_t optvallen);
NN_EXPORT int nn_getsockopt (int s, int level, int option, void *optval,
    size_t *optvallen);
NN_EXPORT int nn_bind (int s, const char *addr);
NN_EXPORT int nn_connect (int s, const char *addr);
NN_EXPORT int nn_shutdown (int s, int how);
NN_EXPORT int nn_send (int s, const void *buf, size_t len, int flags);
NN_EXPORT int nn_recv (int s, void *buf, size_t len, int flags);
NN_EXPORT int nn_sendmsg (int s, const struct nn_msghdr *msghdr, int flags);
NN_EXPORT int nn_recvmsg (int s, struct nn_msghdr *msghdr, int flags);

/******************************************************************************/
/*  Built-in support for devices.                                             */
/******************************************************************************/

NN_EXPORT int nn_device (int s1, int s2);

#undef NN_EXPORT

#ifdef __cplusplus
}
#endif

#endif

