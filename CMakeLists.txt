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
project(nanomsg)
enable_testing()

if(UNIX)
    add_definitions(-DSP_USE_POLL)
    add_definitions(-DSP_USE_EVENTFD)
endif()

if(WIN32)
    add_definitions(-DSP_HAVE_WINDOWS)
endif()

find_package(Threads)

check_include_files (sys/eventfd.h SP_HAVE_EVENTFD)
check_include_files (sys/epoll.h SP_HAVE_EPOLL)
check_include_files (poll.h SP_HAVE_POLL)
check_include_files (ifaddrs.h SP_HAVE_IFADDRS)

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(perf)

install(FILES src/sp.h DESTINATION include/sp)

