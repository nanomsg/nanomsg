#
#   Copyright (c) 2012 250bpm s.r.o.
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

cmake_minimum_required(VERSION 2.8)
include(CheckSymbolExists)

project(nanomsg)
enable_testing()

#  Platform checks.

if(WIN32)
    add_definitions(-DSP_HAVE_WINDOWS)
endif()

if (APPLE)
    add_definitions(-DSP_HAVE_OSX)
endif()

#  Feature checks.

find_package(Threads)

check_include_files(sys/eventfd.h SP_HAVE_EVENTFD)
if(SP_HAVE_EVENTFD)
    add_definitions(-DSP_HAVE_EVENTFD)
endif()

check_symbol_exists(socketpair sys/socket.h SP_HAVE_SOCKETPAIR)
if(SP_HAVE_SOCKETPAIR)
    add_definitions(-DSP_HAVE_SOCKETPAIR)
endif()

check_symbol_exists(CLOCK_MONOTONIC time.h SP_HAVE_CLOCK_MONOTONIC)
if(SP_HAVE_CLOCK_MONOTONIC)
    add_definitions(-DSP_HAVE_CLOCK_MONOTONIC)
endif()

check_symbol_exists(CLOCK_MONOTONIC time.h SP_HAVE_CLOCK_MONOTONIC)
if(SP_HAVE_CLOCK_MONOTONIC)
    add_definitions(-DSP_HAVE_CLOCK_MONOTONIC)
endif()

check_symbol_exists(gethrtime time.h SP_HAVE_GETHRTIME)
if(SP_HAVE_GETHRTIME)
    add_definitions(-DSP_HAVE_GETHRTIME)
endif()

check_include_files(sys/epoll.h SP_HAVE_EPOLL)
if(SP_HAVE_EPOLL)
    add_definitions(-DSP_HAVE_EPOLL)
endif()

check_include_files(poll.h SP_HAVE_POLL)
if(SP_HAVE_POLL)
    add_definitions(-DSP_HAVE_POLL)
endif()

check_include_files(ifaddrs.h SP_HAVE_IFADDRS)
if(SP_HAVE_IFADDRS)
    add_definitions(-DSP_HAVE_IFADDRS)
endif()

list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_symbol_exists(accept4 sys/socket.h SP_HAVE_ACCEPT4)
list(REMOVE_ITEM CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
if(SP_HAVE_ACCEPT4)
    add_definitions(-DSP_HAVE_ACCEPT4)
endif()

#  Decide which features to actually use.

if(SP_HAVE_EPOLL)
    add_definitions(-DSP_USE_EPOLL)
elseif(SP_HAVE_POLL)
    add_definitions(-DSP_USE_POLL)
endif()

if(SP_HAVE_EVENTFD)
    add_definitions(-DSP_USE_EVENTFD)
elseif(SP_HAVE_SOCKETPAIR)
    add_definitions(-DSP_USE_SOCKETPAIR)
endif()

if(SP_HAVE_IFADDRS)
    add_definitions(-DSP_USE_IFADDRS)
endif()

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(perf)

install(FILES src/sp.h DESTINATION include/sp)

