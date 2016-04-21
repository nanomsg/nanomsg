#
#   Copyright (c) 2012 Martin Sustrik  All rights reserved.
#   Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
#   Copyright (c) 2015-2016 Jack R. Dunaway. All rights reserved.
#   Copyright 2016 Garrett D'Amore <garrett@damore.org>
#   Copyright 2016 Franklin "Snaipe" Mathieu <franklinmathieu@gmail.com>
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"),
#   to deal in the Software without restriction, including without limitation
#   the rights to use, copy, modify, merge, publish, distribute, sublicense,
#   and/or sell copies of the Software, and to permit persons to whom
#   the Software is furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
#   IN THE SOFTWARE.
#

cmake_minimum_required (VERSION 2.8.7)
include (CheckFunctionExists)
include (CheckSymbolExists)
include (CheckStructHasMember)
include (CheckLibraryExists)
include (CheckCSourceCompiles)

project (nanomsg C)

set (ISSUE_REPORT_MSG "Please consider opening an issue at https://github.com/nanomsg/nanomsg with your findings")

# Determine library versions.

file (READ src/nn.h NN_HDR_STR)
string (REGEX REPLACE ".*#define +NN_VERSION_CURRENT +([0-9]+).*" "\\1" NN_VERSION_CURRENT "${NN_HDR_STR}")
string (REGEX REPLACE ".*#define +NN_VERSION_REVISION +([0-9]+).*" "\\1" NN_VERSION_REVISION "${NN_HDR_STR}")
string (REGEX REPLACE ".*#define +NN_VERSION_AGE +([0-9]+).*" "\\1" NN_VERSION_AGE "${NN_HDR_STR}")

if ((NN_VERSION_CURRENT STREQUAL "") OR (NN_VERSION_REVISION STREQUAL "") OR (NN_VERSION_AGE STREQUAL ""))
    message (FATAL_ERROR "Could not read ABI version from nn.h")
else ()
    set (NN_ABI_VERSION "${NN_VERSION_CURRENT}.${NN_VERSION_REVISION}.${NN_VERSION_AGE}")
    message (STATUS "Detected nanomsg ABI v${NN_ABI_VERSION}")
endif ()

# Determine package version.
find_package (Git QUIET)
if (DEFINED ENV{TRAVIS_TAG})
    set (NN_PACKAGE_VERSION "$ENV{TRAVIS_TAG}")
elseif (GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Working off a git repo, using git versioning

    # Get version from last tag
    execute_process (
        COMMAND             "${GIT_EXECUTABLE}" describe --always# | sed -e "s:v::"
        WORKING_DIRECTORY   "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE     NN_PACKAGE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    # If the sources have been changed locally, add -dirty to the version.
    execute_process (
        COMMAND             "${GIT_EXECUTABLE}" diff --quiet
        WORKING_DIRECTORY   "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE     res)
    if (res EQUAL 1)
        set (NN_PACKAGE_VERSION "${NN_PACKAGE_VERSION}-dirty")
    endif()

elseif (EXISTS .version)
    #  If git is not available (e.g. when building from source package)
    #  we can extract the package version from .version file.
    file (READ .version NN_PACKAGE_VERSION)
else ()
    set (NN_PACKAGE_VERSION "Unknown")
endif()

# User-defined options.

option (NN_STATIC_LIB "Build static library instead of shared library." OFF)
option (NN_ENABLE_GETADDRINFO_A "Enable/disable use of getaddrinfo_a in place of getaddrinfo." ON)
option (NN_TESTS "Build and run nanomsg tests" ON)
option (NN_TOOLS "Build nanomsg tools" ON)
option (NN_ENABLE_NANOCAT "Enable building nanocat utility." ${NN_TOOLS})

#  Platform checks.

find_package (Threads REQUIRED)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_definitions (-DNN_HAVE_LINUX)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    add_definitions (-DNN_HAVE_OSX)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    SET (NN_HAVE_WINSOCK 1)
    add_definitions (-DNN_HAVE_WINDOWS)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS)
elseif (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    add_definitions (-DNN_HAVE_FREEBSD)
elseif (CMAKE_SYSTEM_NAME MATCHES "NetBSD")
    add_definitions (-DNN_HAVE_NETBSD)
elseif (CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
    add_definitions (-DNN_HAVE_OPENBSD)
elseif (CMAKE_SYSTEM_NAME MATCHES "Solaris|SunOS")
    add_definitions (-DNN_HAVE_SOLARIS)
elseif (CMAKE_SYSTEM_NAME MATCHES "QNX")
    add_definitions (-DNN_HAVE_QNX)
else ()
    message (AUTHOR_WARNING "WARNING: This platform may or may not be supported: ${CMAKE_SYSTEM_NAME}")
    message (AUTHOR_WARNING "${ISSUE_REPORT_MSG}")
endif ()

macro (nn_check_func SYM DEF)
    check_function_exists (${SYM} ${DEF})
     if (${DEF})
         add_definitions (-D${DEF}=1)
     endif ()
endmacro (nn_check_func)

macro (nn_check_sym SYM HDR DEF)
    check_symbol_exists (${SYM} ${HDR} ${DEF})
     if (${DEF})
         add_definitions (-D${DEF}=1)
     endif ()
endmacro (nn_check_sym)

macro (nn_check_lib LIB SYM DEF)
    check_library_exists (${LIB} ${SYM} "" ${DEF})
    if (${DEF})
        add_definitions (-D${DEF}=1)
        set(NN_REQUIRED_LIBRARIES ${NN_REQUIRED_LIBRARIES} ${LIB})
    endif ()
endmacro (nn_check_lib)

macro (nn_check_struct_member STR MEM HDR DEF)
    check_struct_has_member ("struct ${STR}" ${MEM} ${HDR} ${DEF})
    if (${DEF})
        add_definitions (-D${DEF}=1)
    endif ()
endmacro (nn_check_struct_member)

# Unconditionally declare the following feature test macros.  These are
# needed for some platforms (glibc and SunOS/illumos) and should be harmless
# on the others.
add_definitions (-D_GNU_SOURCE)
add_definitions (-D_REENTRANT)
add_definitions (-D_THREAD_SAFE)
add_definitions (-D_POSIX_PTHREAD_SEMANTICS)

nn_check_func (gethrtime NN_HAVE_GETHRTIME)
nn_check_func (socketpair NN_HAVE_SOCKETPAIR)
nn_check_func (eventfd NN_HAVE_EVENTFD)
nn_check_func (pipe NN_HAVE_PIPE)
nn_check_func (pipe2 NN_HAVE_PIPE2)
nn_check_func (accept4 NN_HAVE_ACCEPT4)
nn_check_func (epoll_create NN_HAVE_EPOLL)
nn_check_func (kqueue NN_HAVE_KQUEUE)
nn_check_func (poll NN_HAVE_POLL)

nn_check_lib (anl getaddrinfo_a NN_HAVE_GETADDRINFO_A)
nn_check_lib (rt clock_gettime  NN_HAVE_CLOCK_GETTIME)
nn_check_lib (rt sem_wait NN_HAVE_SEMAPHORE_RT)
nn_check_lib (pthread sem_wait  NN_HAVE_SEMAPHORE_PTHREAD)

nn_check_lib (nsl gethostbyname NN_HAVE_LIBNSL)
nn_check_lib (socket socket NN_HAVE_LIBSOCKET)
nn_check_sym (CLOCK_MONOTONIC time.h NN_HAVE_CLOCK_MONOTONIC)
nn_check_sym (atomic_cas_32 atomic.h NN_HAVE_ATOMIC_SOLARIS)

nn_check_struct_member(msghdr msg_control sys/socket.h NN_HAVE_MSG_CONTROL)

if (NN_HAVE_SEMAPHORE_RT OR NN_HAVE_SEMAPHORE_PTHREAD)
    add_definitions (-DNN_HAVE_SEMAPHORE)
endif ()

if (NOT NN_ENABLE_GETADDRINFO_A)
    add_definitions (-DNN_DISABLE_GETADDRINFO_A)
endif ()

check_c_source_compiles ("
    #include <stdint.h>
    int main()
    {
        volatile uint32_t n = 0;
        __sync_fetch_and_add (&n, 1);
        __sync_fetch_and_sub (&n, 1);
        return 0;
    }
" NN_HAVE_GCC_ATOMIC_BUILTINS)
if (NN_HAVE_GCC_ATOMIC_BUILTINS)
    add_definitions (-DNN_HAVE_GCC_ATOMIC_BUILTINS)
endif ()

add_subdirectory (src)

#  Build the tools

if (NN_ENABLE_NANOCAT)
    add_executable (nanocat tools/nanocat.c tools/options.c)
    target_link_libraries (nanocat ${PROJECT_NAME})
endif ()

#  Build unit tests.

if (NN_TESTS)

    enable_testing ()
    set (all_tests "")

    set (TEST_PORT 5500)
    macro (add_libnanomsg_test NAME TIMEOUT)
        list (APPEND all_tests ${NAME})
        add_executable (${NAME} tests/${NAME}.c)
        target_link_libraries (${NAME} ${PROJECT_NAME})
        add_test (NAME ${NAME} COMMAND ${NAME} ${TEST_PORT})
        set_tests_properties (${NAME} PROPERTIES TIMEOUT ${TIMEOUT})
        math (EXPR TEST_PORT "${TEST_PORT}+10")
    endmacro (add_libnanomsg_test)

    #  Transport tests.
    add_libnanomsg_test (inproc 5)
    add_libnanomsg_test (inproc_shutdown 5)
    add_libnanomsg_test (ipc 5)
    add_libnanomsg_test (ipc_shutdown 30)
    add_libnanomsg_test (ipc_stress 5)
    add_libnanomsg_test (tcp 5)
    add_libnanomsg_test (tcp_shutdown 30)
    add_libnanomsg_test (ws 5)

    #  Protocol tests.
    add_libnanomsg_test (pair 5)
    add_libnanomsg_test (pubsub 5)
    add_libnanomsg_test (reqrep 5)
    add_libnanomsg_test (pipeline 5)
    add_libnanomsg_test (survey 5)
    add_libnanomsg_test (bus 5)

    #  Feature tests.
    add_libnanomsg_test (async_shutdown 30)
    add_libnanomsg_test (block 5)
    add_libnanomsg_test (term 5)
    add_libnanomsg_test (timeo 5)
    add_libnanomsg_test (iovec 5)
    add_libnanomsg_test (msg 5)
    add_libnanomsg_test (prio 5)
    add_libnanomsg_test (poll 5)
    add_libnanomsg_test (device 5)
    add_libnanomsg_test (device4 5)
    add_libnanomsg_test (device5 5)
    add_libnanomsg_test (device6 5)
    add_libnanomsg_test (device7 30)
    add_libnanomsg_test (emfile 5)
    add_libnanomsg_test (domain 5)
    add_libnanomsg_test (trie 5)
    add_libnanomsg_test (list 5)
    add_libnanomsg_test (hash 5)
    add_libnanomsg_test (stats 5)
    add_libnanomsg_test (symbol 5)
    add_libnanomsg_test (separation 5)
    add_libnanomsg_test (zerocopy 5)
    add_libnanomsg_test (shutdown 5)
    add_libnanomsg_test (cmsg 5)
    add_libnanomsg_test (bug328 5)

    # Platform-specific tests
    if (WIN32)
        add_libnanomsg_test (win_sec_attr 5)
    endif()

    #  Build the performance tests.

    macro (add_libnanomsg_perf NAME)
        add_executable (${NAME} perf/${NAME}.c)
        target_link_libraries (${NAME} ${PROJECT_NAME})
    endmacro (add_libnanomsg_perf)

    add_libnanomsg_perf (inproc_lat)
    add_libnanomsg_perf (inproc_thr)
    add_libnanomsg_perf (local_lat)
    add_libnanomsg_perf (remote_lat)
    add_libnanomsg_perf (local_thr)
    add_libnanomsg_perf (remote_thr)

endif ()

#  NSIS package

install (FILES src/nn.h DESTINATION include/nanomsg)
install (FILES src/inproc.h DESTINATION include/nanomsg)
install (FILES src/ipc.h DESTINATION include/nanomsg)
install (FILES src/tcp.h DESTINATION include/nanomsg)
install (FILES src/ws.h DESTINATION include/nanomsg)
install (FILES src/pair.h DESTINATION include/nanomsg)
install (FILES src/pubsub.h DESTINATION include/nanomsg)
install (FILES src/reqrep.h DESTINATION include/nanomsg)
install (FILES src/pipeline.h DESTINATION include/nanomsg)
install (FILES src/survey.h DESTINATION include/nanomsg)
install (FILES src/bus.h DESTINATION include/nanomsg)

if (NN_ENABLE_NANOCAT)
    install (TARGETS nanocat RUNTIME DESTINATION bin)
endif()

set (CPACK_GENERATOR "NSIS")
set (CPACK_PACKAGE_NAME ${PROJECT_NAME})
include (CPack)
