#
#   Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.
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

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set (all_tests "")

macro (add_libnanomsg_test NAME)
    list (APPEND all_tests ${NAME})
    add_executable (${NAME} ${NAME}.c)
    target_link_libraries (${NAME} nanomsg)
    add_test (${NAME} ${CMAKE_BINARY_DIR}/${NAME})
endmacro (add_libnanomsg_test)

#  Transport tests.
add_libnanomsg_test (inproc)
add_libnanomsg_test (inproc_shutdown)
add_libnanomsg_test (ipc)
add_libnanomsg_test (ipc_shutdown)
add_libnanomsg_test (tcp)
add_libnanomsg_test (tcp_shutdown)

#  Protocol tests.
add_libnanomsg_test (pair)
add_libnanomsg_test (pubsub)
add_libnanomsg_test (reqrep)
add_libnanomsg_test (fanin)
add_libnanomsg_test (fanout)
add_libnanomsg_test (survey)
add_libnanomsg_test (bus)

#  Feature tests.
add_libnanomsg_test (block)
add_libnanomsg_test (shutdown)
add_libnanomsg_test (timeo)
add_libnanomsg_test (iovec)
add_libnanomsg_test (msg)
add_libnanomsg_test (prio)
add_libnanomsg_test (poll)
add_libnanomsg_test (device)
add_libnanomsg_test (emfile)
add_libnanomsg_test (domain)
add_libnanomsg_test (trie)
add_libnanomsg_test (list)
add_libnanomsg_test (hash)
add_libnanomsg_test (symbol)
add_libnanomsg_test (separation)

#  The builtin "test" target cannot be modified to build the library and tests
#  or to show output when a test failes.
add_custom_target (check
                   COMMENT "Running tests..."
                   COMMAND ctest --force-new-ctest-process --output-on-failure
                   WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_dependencies (check ${all_tests})
