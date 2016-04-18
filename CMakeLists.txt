#
#   Copyright (c) 2012 Martin Sustrik  All rights reserved.
#   Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
#   Copyright (c) 2015-2016 Jack R. Dunaway. All rights reserved.
#   Copyright 2016 Garrett D'Amore <garrett@damore.org>
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
include (CheckLibraryExists)
include (CheckCSourceCompiles)

project (nanomsg C)
enable_testing ()

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

option (ENABLE_NANOCAT "Enable building nanocat utility." OFF)
option (NN_STATIC_LIB "Build static library instead of shared library." OFF)
option (ENABLE_GETADDRINFO_A "Enable/disable use of getaddrinfo_a in place of getaddrinfo." ON)

#  Platform checks.

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_definitions (-DNN_HAVE_LINUX)
    find_package (Threads REQUIRED)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    add_definitions (-DNN_HAVE_OSX)
    find_package (Threads REQUIRED)
elseif (CMAKE_SYSTEM_NAME MATCHES "Windows")
    SET (NN_HAVE_WINSOCK 1)
    add_definitions (-DNN_HAVE_WINDOWS)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS)
elseif (CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
    add_definitions (-DNN_HAVE_FREEBSD)
    find_package (Threads REQUIRED)
elseif (CMAKE_SYSTEM_NAME MATCHES "NetBSD")
    add_definitions (-DNN_HAVE_NETBSD)
    find_package (Threads REQUIRED)
elseif (CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
    add_definitions (-DNN_HAVE_OPENBSD)
    find_package (Threads REQUIRED)
elseif (CMAKE_SYSTEM_NAME MATCHES "Solaris|SunOS")
    add_definitions (-DNN_HAVE_SOLARIS)
    find_package (Threads REQUIRED)
    check_library_exists (socket socket "" NN_HAVE_SOLARIS_SOCKET)
    check_library_exists (nsl gethostbyname "" NN_HAVE_SOLARIS_GETHOSTBYNAME)
elseif (CMAKE_SYSTEM_NAME MATCHES "QNX")
    add_definitions (-DNN_HAVE_QNX)
    find_package (Threads REQUIRED)
    check_library_exists (socket socket "" NN_HAVE_QNX_SOCKET)
else ()
    message (AUTHOR_WARNING "WARNING: This platform may or may not be supported: ${CMAKE_SYSTEM_NAME}")
    message (AUTHOR_WARNING "${ISSUE_REPORT_MSG}")
endif ()

# Compiler checks.

if (CMAKE_C_COMPILER_ID MATCHES "GNU")
    add_definitions (-DNN_HAVE_GCC)
elseif (CMAKE_C_COMPILER_ID MATCHES "Intel")
    add_definitions (-DNN_HAVE_ICC)
elseif (CMAKE_C_COMPILER_ID MATCHES "SunPro")
    add_definitions (-DNN_HAVE_SUN_STUDIO)
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_definitions (-DNN_HAVE_CLANG)
elseif (CMAKE_C_COMPILER_ID MATCHES "HP")
    add_definitions (-DNN_HAVE_HPACC)
elseif (CMAKE_C_COMPILER_ID MATCHES "MSVC")
    add_definitions (-DNN_HAVE_MSVC)
elseif (MINGW)
    message (FATAL_ERROR "ERROR: MinGW is officially no longer supported.")
else ()
    message (AUTHOR_WARNING "WARNING: This compiler may or may not be supported: ${CMAKE_C_COMPILER_ID}")
    message (AUTHOR_WARNING "${ISSUE_REPORT_MSG}")
endif ()

check_function_exists (gethrtime NN_HAVE_GETHRTIME)
check_function_exists (socketpair NN_HAVE_SOCKETPAIR)
check_function_exists (eventfd NN_HAVE_EVENTFD)
check_function_exists (pipe NN_HAVE_PIPE)
check_function_exists (pipe2 NN_HAVE_PIPE2)
check_function_exists (accept4 NN_HAVE_ACCEPT4)
check_function_exists (epoll_create NN_HAVE_EPOLL)
check_function_exists (kqueue NN_HAVE_KQUEUE)
check_function_exists (poll NN_HAVE_POLL)

check_library_exists (anl getaddrinfo_a "" NN_HAVE_GETADDRINFO_A)
check_library_exists (rt clock_gettime "" NN_HAVE_CLOCK_GETTIME)
check_library_exists (rt sem_wait "" NN_HAVE_SEMAPHORE_RT)
check_library_exists (pthread sem_wait  "" NN_HAVE_SEMAPHORE_PTHREAD)

if (NN_HAVE_POLL)
    add_definitions (-DNN_HAVE_POLL)
endif ()

if (NN_HAVE_SEMAPHORE_RT OR NN_HAVE_SEMAPHORE_PTHREAD)
    add_definitions (-DNN_HAVE_SEMAPHORE)
endif ()

if (NOT ENABLE_GETADDRINFO_A)
    add_definitions (-DNN_DISABLE_GETADDRINFO_A)
endif ()

if (NN_HAVE_PIPE2 OR NN_HAVE_ACCEPT4 OR NN_HAVE_GETADDRINFO_A)
    add_definitions (-D_GNU_SOURCE)
endif ()

check_c_source_compiles ("
    #include <stdint.h>
    int main()
    {
        int64_t i;
        i = 0;
        return 0;
    }
" NN_HAVE_STDINT)
if (NN_HAVE_STDINT)
    add_definitions (-DNN_HAVE_STDINT)
endif ()

check_c_source_compiles ("
    #include <time.h>
    int main()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return 0;
    }
" NN_HAVE_CLOCK_MONOTONIC)
if (NN_HAVE_CLOCK_MONOTONIC)
    add_definitions (-DNN_HAVE_CLOCK_MONOTONIC)
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

check_c_source_compiles ("
    #include <atomic.h>
    int main()
    {
        uint32_t value;
        atomic_cas_32 (&value, 0, 0);
        return 0;
    }
" NN_HAVE_ATOMIC_SOLARIS)
if (NN_HAVE_ATOMIC_SOLARIS)
    add_definitions (-DNN_HAVE_ATOMIC_SOLARIS)
endif ()

check_c_source_compiles ("
    #include <sys/socket.h>
    int main()
    {
        struct msghdr hdr;
        hdr.msg_control = 0;
        return 0;
    }
" NN_HAVE_MSG_CONTROL)
if (NN_HAVE_MSG_CONTROL)
    add_definitions (-DNN_HAVE_MSG_CONTROL)
endif ()

add_subdirectory (src)

#  Build the tools

if (ENABLE_NANOCAT)
    add_executable (nanocat tools/nanocat.c tools/options.c)
    target_link_libraries (nanocat ${PROJECT_NAME})
endif ()

#  Build unit tests.

set (all_tests "")

macro (add_libnanomsg_test NAME TIMEOUT)
    list (APPEND all_tests ${NAME})
    add_executable (${NAME} tests/${NAME}.c)
    target_link_libraries (${NAME} ${PROJECT_NAME})
    add_test (NAME ${NAME} COMMAND ${NAME})
    set_tests_properties (${NAME} PROPERTIES TIMEOUT ${TIMEOUT})
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
add_libnanomsg_test (tcpmux 5)

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

if (BUILD_NANOCAT)
    install (TARGETS nanocat RUNTIME DESTINATION bin)
endif()

set (CPACK_GENERATOR "NSIS")
set (CPACK_PACKAGE_NAME ${PROJECT_NAME})
include (CPack)
