#
#   Copyright (c) 2012-2013 250bpm s.r.o.  All rights reserved.
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

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set (NN_SOURCES
    ../src/nn.h
    ../src/inproc.h
    ../src/ipc.h
    ../src/tcp.h
    ../src/pair.h
    ../src/pubsub.h
    ../src/reqrep.h
    ../src/pipeline.h
    ../src/survey.h
    ../src/bus.h

    ../src/core/ep.h
    ../src/core/ep.c
    ../src/core/epbase.c
    ../src/core/global.h
    ../src/core/global.c
    ../src/core/pipe.c
    ../src/core/poll.c
    ../src/core/sock.h
    ../src/core/sock.c
    ../src/core/sockbase.c
    ../src/core/device.c
    ../src/core/symbol.c

    ../src/aio/ctx.h
    ../src/aio/ctx.c
    ../src/aio/fsm.h
    ../src/aio/fsm.c
    ../src/aio/iocp.h
    ../src/aio/iocp.c
    ../src/aio/poller.h
    ../src/aio/poller.c
    ../src/aio/poller_epoll.h
    ../src/aio/poller_epoll.inc
    ../src/aio/poller_kqueue.h
    ../src/aio/poller_kqueue.inc
    ../src/aio/poller_poll.h
    ../src/aio/poller_poll.inc
    ../src/aio/pool.h
    ../src/aio/pool.c
    ../src/aio/timer.h
    ../src/aio/timer.c
    ../src/aio/timerset.h
    ../src/aio/timerset.c
    ../src/aio/usock.h
    ../src/aio/usock.c
    ../src/aio/usock_posix.h
    ../src/aio/usock_posix.inc
    ../src/aio/usock_win.h
    ../src/aio/usock_win.inc
    ../src/aio/worker.h
    ../src/aio/worker.c
    ../src/aio/worker_posix.h
    ../src/aio/worker_posix.inc
    ../src/aio/worker_win.h
    ../src/aio/worker_win.inc

    ../src/utils/alloc.h
    ../src/utils/alloc.c
    ../src/utils/atomic.h
    ../src/utils/atomic.c
    ../src/utils/attr.h
    ../src/utils/chunk.h
    ../src/utils/chunk.c
    ../src/utils/chunkref.h
    ../src/utils/chunkref.c
    ../src/utils/clock.h
    ../src/utils/clock.c
    ../src/utils/closefd.h
    ../src/utils/closefd.c
    ../src/utils/cont.h
    ../src/utils/efd.h
    ../src/utils/efd.c
    ../src/utils/efd_eventfd.h
    ../src/utils/efd_eventfd.inc
    ../src/utils/efd_pipe.h
    ../src/utils/efd_pipe.inc
    ../src/utils/efd_socketpair.h
    ../src/utils/efd_socketpair.inc
    ../src/utils/efd_win.h
    ../src/utils/efd_win.inc
    ../src/utils/err.h
    ../src/utils/err.c
    ../src/utils/fast.h
    ../src/utils/fd.h
    ../src/utils/glock.h
    ../src/utils/glock.c
    ../src/utils/hash.h
    ../src/utils/hash.c
    ../src/utils/int.h
    ../src/utils/list.h
    ../src/utils/list.c
    ../src/utils/msg.h
    ../src/utils/msg.c
    ../src/utils/mutex.h
    ../src/utils/mutex.c
    ../src/utils/queue.h
    ../src/utils/queue.c
    ../src/utils/random.h
    ../src/utils/random.c
    ../src/utils/sem.h
    ../src/utils/sem.c
    ../src/utils/sleep.h
    ../src/utils/sleep.c
    ../src/utils/thread.h
    ../src/utils/thread.c
    ../src/utils/thread_posix.h
    ../src/utils/thread_posix.inc
    ../src/utils/thread_win.h
    ../src/utils/thread_win.inc
    ../src/utils/wire.h
    ../src/utils/wire.c

    ../src/protocols/utils/dist.h
    ../src/protocols/utils/dist.c
    ../src/protocols/utils/excl.h
    ../src/protocols/utils/excl.c
    ../src/protocols/utils/fq.h
    ../src/protocols/utils/fq.c
    ../src/protocols/utils/lb.h
    ../src/protocols/utils/lb.c
    ../src/protocols/utils/priolist.h
    ../src/protocols/utils/priolist.c

    ../src/protocols/bus/bus.h
    ../src/protocols/bus/bus.c
    ../src/protocols/bus/xbus.h
    ../src/protocols/bus/xbus.c

    ../src/protocols/pipeline/push.h
    ../src/protocols/pipeline/push.c
    ../src/protocols/pipeline/pull.h
    ../src/protocols/pipeline/pull.c
    ../src/protocols/pipeline/xpull.h
    ../src/protocols/pipeline/xpull.c
    ../src/protocols/pipeline/xpush.h
    ../src/protocols/pipeline/xpush.c

    ../src/protocols/pair/pair.h
    ../src/protocols/pair/pair.c
    ../src/protocols/pair/xpair.h
    ../src/protocols/pair/xpair.c

    ../src/protocols/pubsub/pub.h
    ../src/protocols/pubsub/pub.c
    ../src/protocols/pubsub/sub.h
    ../src/protocols/pubsub/sub.c
    ../src/protocols/pubsub/trie.h
    ../src/protocols/pubsub/trie.c
    ../src/protocols/pubsub/xpub.h
    ../src/protocols/pubsub/xpub.c
    ../src/protocols/pubsub/xsub.h
    ../src/protocols/pubsub/xsub.c

    ../src/protocols/reqrep/req.h
    ../src/protocols/reqrep/req.c
    ../src/protocols/reqrep/rep.h
    ../src/protocols/reqrep/rep.c
    ../src/protocols/reqrep/xrep.h
    ../src/protocols/reqrep/xrep.c
    ../src/protocols/reqrep/xreq.h
    ../src/protocols/reqrep/xreq.c

    ../src/protocols/survey/respondent.h
    ../src/protocols/survey/respondent.c
    ../src/protocols/survey/surveyor.h
    ../src/protocols/survey/surveyor.c
    ../src/protocols/survey/xrespondent.h
    ../src/protocols/survey/xrespondent.c
    ../src/protocols/survey/xsurveyor.h
    ../src/protocols/survey/xsurveyor.c

    ../src/transports/utils/backoff.h
    ../src/transports/utils/backoff.c
    ../src/transports/utils/dns.h
    ../src/transports/utils/dns.c
    ../src/transports/utils/dns_getaddrinfo.h
    ../src/transports/utils/dns_getaddrinfo.inc
    ../src/transports/utils/dns_getaddrinfo_a.h
    ../src/transports/utils/dns_getaddrinfo_a.inc
    ../src/transports/utils/iface.h
    ../src/transports/utils/iface.c
    ../src/transports/utils/literal.h
    ../src/transports/utils/literal.c
    ../src/transports/utils/port.h
    ../src/transports/utils/port.c
    ../src/transports/utils/streamhdr.h
    ../src/transports/utils/streamhdr.c

    ../src/transports/inproc/binproc.h
    ../src/transports/inproc/binproc.c
    ../src/transports/inproc/cinproc.h
    ../src/transports/inproc/cinproc.c
    ../src/transports/inproc/inproc.h
    ../src/transports/inproc/inproc.c
    ../src/transports/inproc/ins.h
    ../src/transports/inproc/ins.c
    ../src/transports/inproc/msgqueue.h
    ../src/transports/inproc/msgqueue.c
    ../src/transports/inproc/sinproc.h
    ../src/transports/inproc/sinproc.c

    ../src/transports/ipc/aipc.h
    ../src/transports/ipc/aipc.c
    ../src/transports/ipc/bipc.h
    ../src/transports/ipc/bipc.c
    ../src/transports/ipc/cipc.h
    ../src/transports/ipc/cipc.c
    ../src/transports/ipc/ipc.h
    ../src/transports/ipc/ipc.c
    ../src/transports/ipc/sipc.h
    ../src/transports/ipc/sipc.c

    ../src/transports/tcp/atcp.h
    ../src/transports/tcp/atcp.c
    ../src/transports/tcp/btcp.h
    ../src/transports/tcp/btcp.c
    ../src/transports/tcp/ctcp.h
    ../src/transports/tcp/ctcp.c
    ../src/transports/tcp/stcp.h
    ../src/transports/tcp/stcp.c
    ../src/transports/tcp/tcp.h
    ../src/transports/tcp/tcp.c
)

if (WIN32)
    LIST (APPEND NN_SOURCES
        ../src/utils/win.h
    )
endif ()

add_library (nanomsg_xp STATIC ${NN_SOURCES})
add_definitions (-DNN_NO_EXPORTS)

add_definitions (-DNN_USE_LITERAL_IFADDR)
add_definitions(-DUNICODE -D_UNICODE)
add_definitions (-DSIM_XP)

target_link_libraries (nanomsg_xp ws2_32)
target_link_libraries (nanomsg_xp Mswsock.lib)
target_link_libraries (nanomsg_xp Advapi32.lib)
    
    
set(CMAKE_C_FLAGS_DEBUG          "/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1" )
set(CMAKE_C_FLAGS_MINSIZEREL     "/MT /O1 /Ob1 /D NDEBUG"           )
set(CMAKE_C_FLAGS_RELEASE        "/MT /O2 /Ob2 /D NDEBUG"           )
set(CMAKE_C_FLAGS_RELWITHDEBINFO "/MT /Zi /O2 /Ob1 /D NDEBUG"       )

install(TARGETS nanomsg_xp
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)

