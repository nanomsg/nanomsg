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

#ifndef SP_H_INCLUDED
#define SP_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <stddef.h>

/*  Handle DSO symbol visibility                                             */
#if defined _WIN32
#   if defined SP_EXPORTS
#       define SP_EXPORT __declspec(dllexport)
#   else
#       define SP_EXPORT __declspec(dllimport)
#   endif
#else
#   if defined __SUNPRO_C  || defined __SUNPRO_CC
#       define SP_EXPORT __global
#   elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#       define SP_EXPORT __attribute__ ((visibility("default")))
#   else
#       define SP_EXPORT
#   endif
#endif

/******************************************************************************/
/*  Versioning support.                                                       */
/******************************************************************************/

/*  Version macros for compile-time API version detection                     */
#define SP_VERSION_MAJOR 0
#define SP_VERSION_MINOR 0
#define SP_VERSION_PATCH 0

#define SP_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define SP_VERSION \
    SP_MAKE_VERSION(SP_VERSION_MAJOR, SP_VERSION_MINOR, SP_VERSION_PATCH)

/*  Run-time API version detection                                            */
SP_EXPORT void sp_version (int *major, int *minor, int *patch);

/******************************************************************************/
/*  Errors.                                                                   */
/******************************************************************************/

/*  A number random enough not to collide with different errno ranges on      */
/*  different OSes. The assumption is that error_t is at least 32-bit type.   */
#define SP_HAUSNUMERO 156384712

/*  On Windows platform some of the standard POSIX errnos are not defined.    */
#ifndef ENOTSUP
#define ENOTSUP (SP_HAUSNUMERO + 1)
#define SP_ENOTSUP_DEFINED
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (SP_HAUSNUMERO + 2)
#define SP_EPROTONOSUPPORT_DEFINED
#endif
#ifndef ENOBUFS
#define ENOBUFS (SP_HAUSNUMERO + 3)
#define SP_ENOBUFS_DEFINED
#endif
#ifndef ENETDOWN
#define ENETDOWN (SP_HAUSNUMERO + 4)
#define SP_ENETDOWN_DEFINED
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (SP_HAUSNUMERO + 5)
#define SP_EADDRINUSE_DEFINED
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (SP_HAUSNUMERO + 6)
#define SP_EADDRNOTAVAIL_DEFINED
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (SP_HAUSNUMERO + 7)
#define SP_ECONNREFUSED_DEFINED
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (SP_HAUSNUMERO + 8)
#define SP_EINPROGRESS_DEFINED
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (SP_HAUSNUMERO + 9)
#define SP_ENOTSOCK_DEFINED
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (SP_HAUSNUMERO + 10)
#define SP_EAFNOSUPPORT_DEFINED
#endif

/*  Native error codes.                                                       */
#define ETERM (SP_HAUSNUMERO + 53)
#define EFSM (SP_HAUSNUMERO + 54)

/*  This function retrieves the errno as it is known to the library.          */
/*  The goal of this function is to make the code 100% portable, including    */
/*  where the library is compiled with certain CRT library (on Windows) and   */
/*  linked to an application that uses different CRT library.                 */
SP_EXPORT int sp_errno (void);

/*  Resolves system errors and native errors to human-readable string.        */
SP_EXPORT const char *sp_strerror (int errnum);

/******************************************************************************/
/*  Initialisation and shutdown.                                              */
/******************************************************************************/

SP_EXPORT int sp_init (void);
SP_EXPORT int sp_term (void);

/******************************************************************************/
/*  SP socket definition.                                                     */
/******************************************************************************/

/*  SP address families.                                                      */
#define AF_SP 1
#define AF_SP_RAW 2

/*  Max size of an SP address.                                                */
#define SP_SOCKADDR_MAX 128

/*  Socket protocols.                                                         */
#define SP_PAIR 1
#define SP_PUB 2
#define SP_SUB 3
#define SP_REP 4
#define SP_REQ 5
#define SP_SINK 6
#define SP_SOURCE 7
#define SP_PUSH 8
#define SP_PULL 9

/*  Socket option levels.                                                     */
#define SP_SOL_SOCKET 1

/*  Socket options.                                                           */
#define SP_SUBSCRIBE 1
#define SP_UNSUBSCRIBE 2
#define SP_RESEND_IVL 3

/*  Send/recv options.                                                        */
#define SP_DONTWAIT 1

SP_EXPORT int sp_socket (int domain, int protocol);
SP_EXPORT int sp_close (int s);
SP_EXPORT int sp_setsockopt (int s, int level, int option, const void *optval,
    size_t optvallen); 
SP_EXPORT int sp_getsockopt (int s, int level, int option, void *optval,
    size_t *optvallen);
SP_EXPORT int sp_bind (int s, const char *addr);
SP_EXPORT int sp_connect (int s, const char *addr);
SP_EXPORT int sp_shutdown (int s, int how);
SP_EXPORT int sp_send (int s, const void *buf, size_t len, int flags);
SP_EXPORT int sp_recv (int s, void *buf, size_t len, int flags);

#undef SP_EXPORT

#ifdef __cplusplus
}
#endif

#endif

