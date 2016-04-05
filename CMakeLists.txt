#
#   Copyright (c) 2016 Jerry Jacobs  All rights reserved.
#   Copyright (c) 2012 Martin Sustrik  All rights reserved.
#   Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
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

cmake_minimum_required (VERSION 2.8)
include (CheckIncludeFiles)
include (CheckSymbolExists)
include (CheckCSourceCompiles)
include (CheckFunctionExists)
include (CheckLibraryExists)

project (nanomsg C)
enable_testing ()

#  Platform checks.

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_definitions (-DNN_HAVE_WINDOWS)
    add_definitions (-D_CRT_SECURE_NO_WARNINGS)

    if (MINGW)
        add_definitions (-DNN_HAVE_MINGW -DNN_HAVE_STDINT -D_WIN32_WINNT=0x0600)
    endif ()
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_definitions (-DNN_HAVE_LINUX)
    add_definitions (-DNN_HAVE_MSG_CONTROL) # TODO for some reason it else doesn't compile

    CHECK_FUNCTION_EXISTS(eventfd      HAVE_EVENTFD)
    CHECK_FUNCTION_EXISTS(poll         HAVE_POLL)
    CHECK_FUNCTION_EXISTS(epoll_create HAVE_EPOLL)
    CHECK_FUNCTION_EXISTS(pipe         HAVE_PIPE)
    CHECK_FUNCTION_EXISTS(pipe2        HAVE_PIPE2)

    CHECK_LIBRARY_EXISTS(pthread sem_init                "" HAVE_SEMAPHORE_INIT)
    CHECK_LIBRARY_EXISTS(pthread sem_wait                "" HAVE_SEMAPHORE_WAIT)

    if (${HAVE_EVENTFD})
        add_definitions (-DNN_HAVE_EVENTFD)
    else ()
        message (FATAL_ERROR "ERROR: Platform doesn't support eventfd")
    endif ()

    if (${HAVE_SEMAPHORE_INIT} AND ${HAVE_SEMAPHORE_WAIT})
        add_definitions (-DNN_HAVE_SEMAPHORE)
    else ()
        message (FATAL_ERROR "ERROR: Platform doesn't support semaphore")
    endif ()

    if (${HAVE_POLL})
        add_definitions (-DNN_HAVE_POLL)

        if (${HAVE_EPOLL})
           add_definitions (-DNN_USE_EPOLL)
        endif ()
    else ()
        message (FATAL_ERROR "ERROR: Platform doesn't support poll")
    endif ()

    if (${HAVE_PIPE2})
       add_definitions (-DNN_HAVE_PIPE2)
    elseif (${HAVE_PIPE})
       add_definitions (-DNN_HAVE_PIPE)
    else ()
        message (FATAL_ERROR "ERROR: Platform doesn't support pipe or pipe2")
    endif ()
else ()
    message (FATAL_ERROR "ERROR: CMake build system only works for windows and linux.\nUse autotools build system for any other supported platform.")
endif ()

#  Build the library itself.

add_subdirectory (src)

#  Build the tools

add_executable (nanocat tools/nanocat.c tools/options.c)
target_link_libraries (nanocat nanomsg)

#  Build unit tests.

set (all_tests "")

macro (add_libnanomsg_test NAME TIMEOUT)
    list (APPEND all_tests ${NAME})
    add_executable (${NAME} tests/${NAME}.c)
    target_link_libraries (${NAME} nanomsg)
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
add_libnanomsg_test (tcp_shutdown 120)
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
add_libnanomsg_test (async_shutdown 5)
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
add_libnanomsg_test (device7 120)
add_libnanomsg_test (emfile 5)
add_libnanomsg_test (domain 5)
add_libnanomsg_test (trie 5)
add_libnanomsg_test (list 5)
add_libnanomsg_test (hash 5)
add_libnanomsg_test (symbol 5)
add_libnanomsg_test (separation 5)
add_libnanomsg_test (zerocopy 5)
add_libnanomsg_test (shutdown 5)
add_libnanomsg_test (cmsg 5)
add_libnanomsg_test (bug328 5)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_libnanomsg_test (win_sec_attr 5)
endif ()

#  Build the performance tests.

macro (add_libnanomsg_perf NAME)
    add_executable (${NAME} perf/${NAME}.c)
    target_link_libraries (${NAME} nanomsg)
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

install (TARGETS nanocat RUNTIME DESTINATION bin)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set (CPACK_GENERATOR "NSIS")
endif()

set (CPACK_PACKAGE_NAME "nanomsg")
include (CPack)
