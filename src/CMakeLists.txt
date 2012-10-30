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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_library(nanomsg SHARED
    pattern.h
    sp.h
    transport.h
    core/ctx.h
    core/ctx.c
    core/ep.h
    core/ep.c
    core/pipe.c
    core/sock.h
    core/sock.c
    utils/addr.h
    utils/addr.c
    utils/aio.h
    utils/aio.c
    utils/alloc.h
    utils/alloc.c
    utils/clock.h
    utils/clock.c
    utils/cond.h
    utils/cond.c
    utils/cont.h
    utils/cp.h
    utils/cp.c
    utils/err.h 
    utils/err.c
    utils/excl.h
    utils/excl.c
    utils/fast.h
    utils/glock.h
    utils/glock.c
    utils/hash.h
    utils/hash.c
    utils/list.h
    utils/list.c
    utils/msg.h
    utils/msg.c
    utils/mutex.h
    utils/mutex.c
    utils/poller.h
    utils/poller.c
    utils/random.h
    utils/random.c
    utils/signaler.h
    utils/signaler.c
    utils/sleep.h
    utils/sleep.c
    utils/thread.h
    utils/thread.c
    utils/usock.h
    utils/usock.c
    utils/wire.h
    utils/wire.c
    patterns/pair/pair.h
    patterns/pair/pair.c
    patterns/pubsub/pub.h
    patterns/pubsub/pub.c
    patterns/pubsub/sub.h
    patterns/pubsub/sub.c
    patterns/pubsub/trie.h
    patterns/pubsub/trie.c
    patterns/reqrep/req.h
    patterns/reqrep/req.c
    patterns/reqrep/rep.h
    patterns/reqrep/rep.c
    patterns/reqrep/xrep.h
    patterns/reqrep/xrep.c
    patterns/reqrep/xreq.h
    patterns/reqrep/xreq.c
    transports/inproc/inproc.h
    transports/inproc/inproc_ctx.h
    transports/inproc/inproc_ctx.c
    transports/inproc/inprocb.h
    transports/inproc/inprocb.c
    transports/inproc/inprocc.h
    transports/inproc/inprocc.c
    transports/inproc/msgpipe.h
    transports/inproc/msgpipe.c
    transports/inproc/msgqueue.h
    transports/inproc/msgqueue.c
    transports/tcp/tcp.h
    transports/tcp/tcp.c
    transports/tcp/tcpb.h
    transports/tcp/tcpb.c
    transports/tcp/tcpc.h
    transports/tcp/tcpc.c
    )

add_definitions(-DSP_EXPORTS)

if(UNIX AND NOT APPLE)
    target_link_libraries(nanomsg rt)
endif()

if(WIN32)
    target_link_libraries(nanomsg ws2_32)
    target_link_libraries(nanomsg Mswsock.lib)
endif()

target_link_libraries(nanomsg ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS nanomsg DESTINATION lib)

