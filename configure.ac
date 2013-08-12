# Copyright (c) 2013 Luca Barbato
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

AC_PREREQ([2.53])

AC_INIT([nanomsg], [m4_esyscmd([./version.sh])])
AC_CONFIG_SRCDIR([src/nn.h])
AM_INIT_AUTOMAKE([1.6 foreign subdir-objects dist-xz])
m4_ifdef([AM_SILENT_RULES], [
    AM_SILENT_RULES([yes])
])

AC_CANONICAL_HOST

NN_LT_VERSION=m4_esyscmd([./version.sh -v])
NN_PC_VERSION=m4_esyscmd([./version.sh -p])

AC_SUBST(NN_LT_VERSION)
AC_SUBST(NN_PC_VERSION)

AC_PROG_CC_C99
AM_PROG_CC_C_O

AC_ARG_ENABLE([doc],
    AS_HELP_STRING([--enable-doc], [Build documentation [default=no]])
)

AS_IF([test x"$enable_doc" == "xyes"], [
    AC_CHECK_PROGS([ASCIIDOC], [asciidoc])
    AS_IF([test x"$ASCIIDOC" == "x"],[
        AC_MSG_ERROR([Please install asciidoc to build the documentation])
    ])
    AC_CHECK_PROGS([XMLTO], [xmlto])
    AS_IF([test x"$XMLTO" == "x"], [
        AC_MSG_ERROR([Please install xmlto to build the documentation])
    ])
])
AM_CONDITIONAL([DOC], [test x"$ASCIIDOC" != "x" -a x"$XMLTO" != "x" ])
AM_COND_IF([DOC], [AC_CONFIG_FILES([doc/asciidoc.conf])])

AS_CASE([${host_os}],
    [*linux*],   [AC_DEFINE([NN_HAVE_LINUX])],
    [*mingw32*], [AC_DEFINE([NN_HAVE_WINDOWS])],
    [*darwin*],  [AC_DEFINE([NN_HAVE_OSX])],
    [*freebsd*], [AC_DEFINE([NN_HAVE_FREEBSD])],
    [*netbsd*],  [AC_DEFINE([NN_HAVE_NETBSD])],
    [*openbsd*], [AC_DEFINE([NN_HAVE_OPENBSD])],
    [*solaris*], [
        AC_DEFINE([NN_HAVE_SOLARIS])
        AC_CHECK_LIB([socket], [socket])
        AC_CHECK_LIB([nsl], [gethostbyname])
    ]
)

AX_PTHREAD([
  LIBS="$PTHREAD_LIBS $LIBS"
  CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
  CC="$PTHREAD_CC"
])

AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([netdb.h])
AC_CHECK_HEADERS([arpa/inet.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/ioctl.h])

AC_CHECK_FUNCS([eventfd], [AC_DEFINE([NN_HAVE_EVENTFD])])
AC_CHECK_FUNCS([pipe], [AC_DEFINE([NN_HAVE_PIPE])])
AC_CHECK_FUNCS([pipe2], [
    AC_DEFINE([NN_HAVE_PIPE2])
    CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
])
AC_CHECK_FUNCS([gethrtime], [AC_DEFINE([NN_HAVE_GETHRTIME])])
AC_CHECK_FUNCS([clock_gettime], [
    AC_EGREP_HEADER([_POSIX_MONOTONIC_CLOCK], [unistd.h], [
        AC_DEFINE([NN_HAVE_CLOCK_MONOTONIC])
    ])
])

AC_CHECK_FUNCS([poll], [AC_DEFINE([NN_HAVE_POLL])])

AC_CHECK_FUNCS([epoll_create], [AC_DEFINE([NN_USE_EPOLL])], [
    AC_CHECK_FUNCS([kqueue], [AC_DEFINE([NN_USE_KQUEUE])], [
        AC_DEFINE([NN_USE_POLL])
    ])
])

AC_CHECK_FUNCS([getifaddrs], [AC_DEFINE([NN_USE_IFADDRS])], [
    AC_EGREP_HEADER([SIOCGIFADDR], [sys/ioctl.h], [
        AC_DEFINE([NN_USE_SIOCGIFADDR])
    ], [
        AC_DEFINE([NN_USE_LITERAL_IFADDR])
    ])
])

AC_CHECK_FUNCS([accept4], [
    AC_DEFINE([NN_HAVE_ACCEPT4])
    CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
])
AC_SEARCH_LIBS([getaddrinfo_a], [anl], [
    AC_DEFINE([NN_HAVE_GETADDRINFO_A])
    CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"
])
AC_SEARCH_LIBS([socketpair], [], [
    AC_DEFINE([NN_HAVE_SOCKETPAIR])
])
AC_SEARCH_LIBS([sem_wait], [], [
    AC_DEFINE([NN_HAVE_SEMAPHORE])
])

AC_LINK_IFELSE([AC_LANG_PROGRAM([], [[
        #include <stdint.h>
        int main()
        {
            volatile uint32_t n = 0;
            __sync_fetch_and_add (&n, 1);
            __sync_fetch_and_sub (&n, 1);
            return 0;
        }
]])], [
    AC_DEFINE([NN_HAVE_GCC_ATOMIC_BUILTINS])
])

AS_IF([test x"$ac_cv_func_eventfd"=xyes], [
    AC_DEFINE([NN_USE_EVENTFD])], [
    AS_IF([test x"$ac_cv_func_pipe"=xyes], [
        AC_DEFINE([NN_USE_PIPE])], [
        AS_IF([test x"$ac_cv_func_socketpair"=xyes], [
            AC_DEFINE([NN_USE_SOCKETPAIR])], [
            AC_MSG_ERROR([No signaling supported])
        ])
    ])
])

LT_INIT

DOLT

AC_CONFIG_MACRO_DIR([m4])

AC_OUTPUT([
Makefile
libnanomsg.pc
])
