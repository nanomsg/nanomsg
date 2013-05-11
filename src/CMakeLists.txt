#
#   Copyright (c) 2012-2013 250bpm s.r.o.
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
    nn.h
    transport.h
    protocol.h
    inproc.h
    ipc.h
    tcp.h
    pair.h
    pubsub.h
    reqrep.h
    fanin.h
    fanout.h
    survey.h
    bus.h

    core/ep.h
    core/ep.c
    core/epbase.c
    core/global.h
    core/global.c
    core/pipe.c
    core/sock.h
    core/sock.c
    core/sockbase.c
    core/device.c
    core/symbol.c

    aio/ctx.h
    aio/ctx.c
    aio/fsm.h
    aio/fsm.c
    aio/poller.h
    aio/poller.c
    aio/poller_epoll.inc
    aio/poller_kqueue.inc
    aio/poller_poll.inc
    aio/pool.h
    aio/pool.c
    aio/timer.h
    aio/timer.c
    aio/timerset.h
    aio/timerset.c
    aio/usock.h
    aio/usock.c
    aio/worker.h
    aio/worker.c

    utils/addr.h
    utils/addr.c
    utils/alloc.h
    utils/alloc.c
    utils/atomic.h
    utils/atomic.c
    utils/chunk.h
    utils/chunk.c
    utils/chunkref.h
    utils/chunkref.c
    utils/clock.h
    utils/clock.c
    utils/cont.h
    utils/dist.h
    utils/dist.c
    utils/efd.h
    utils/efd.c
    utils/err.h
    utils/err.c
    utils/excl.h
    utils/excl.c
    utils/fast.h
    utils/fq.h
    utils/fq.c
    utils/glock.h
    utils/glock.c
    utils/hash.h
    utils/hash.c
    utils/lb.h
    utils/lb.c
    utils/list.h
    utils/list.c
    utils/msg.h
    utils/msg.c
    utils/mutex.h
    utils/mutex.c
    utils/priolist.h
    utils/priolist.c
    utils/queue.h
    utils/queue.c
    utils/random.h
    utils/random.c
    utils/sem.h
    utils/sem.c
    utils/sleep.h
    utils/sleep.c
    utils/thread.h
    utils/thread.c
    utils/wire.h
    utils/wire.c

    protocols/bus/bus.h
    protocols/bus/bus.c
    protocols/bus/xbus.h
    protocols/bus/xbus.c

    protocols/fanin/sink.h
    protocols/fanin/sink.c
    protocols/fanin/source.h
    protocols/fanin/source.c
    protocols/fanin/xsink.h
    protocols/fanin/xsink.c
    protocols/fanin/xsource.h
    protocols/fanin/xsource.c

    protocols/fanout/push.h
    protocols/fanout/push.c
    protocols/fanout/pull.h
    protocols/fanout/pull.c
    protocols/fanout/xpull.h
    protocols/fanout/xpull.c
    protocols/fanout/xpush.h
    protocols/fanout/xpush.c

    protocols/pair/pair.h
    protocols/pair/pair.c
    protocols/pair/xpair.h
    protocols/pair/xpair.c

    protocols/pubsub/pub.h
    protocols/pubsub/pub.c
    protocols/pubsub/sub.h
    protocols/pubsub/sub.c
    protocols/pubsub/trie.h
    protocols/pubsub/trie.c

#    protocols/reqrep/req.h
#    protocols/reqrep/req.c
    protocols/reqrep/rep.h
    protocols/reqrep/rep.c
    protocols/reqrep/xrep.h
    protocols/reqrep/xrep.c
    protocols/reqrep/xreq.h
    protocols/reqrep/xreq.c

    protocols/survey/respondent.h
    protocols/survey/respondent.c
#    protocols/survey/surveyor.h
#    protocols/survey/surveyor.c
    protocols/survey/xrespondent.h
    protocols/survey/xrespondent.c
    protocols/survey/xsurveyor.h
    protocols/survey/xsurveyor.c



#    transports/inproc/inproc.h
#    transports/inproc/inproc_ctx.h
#    transports/inproc/inproc_ctx.c
#    transports/inproc/inprocb.h
#    transports/inproc/inprocb.c
#    transports/inproc/inprocc.h
#    transports/inproc/inprocc.c
#    transports/inproc/msgpipe.h
#    transports/inproc/msgpipe.c
#    transports/inproc/msgqueue.h
#    transports/inproc/msgqueue.c

    transports/utils/backoff.h
    transports/utils/backoff.c
    transports/utils/streamhdr.h
    transports/utils/streamhdr.c

    transports/ipc/aipc.h
    transports/ipc/aipc.c
    transports/ipc/bipc.h
    transports/ipc/bipc.c
    transports/ipc/cipc.h
    transports/ipc/cipc.c
    transports/ipc/ipc.h
    transports/ipc/ipc.c
    transports/ipc/sipc.h
    transports/ipc/sipc.c

#    transports/tcp/tcp.h
#    transports/tcp/tcp.c
)

#  Here we cause symbols not to be exported from the library unless
#  they are explicitly marked by NN_EXPORT specifier.
#  Note: For MSVC there's no need for defining a special option. With MSVC
#  symbols are not exported from the library unless explicitly said so.
if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER MATCHES "/(icpc|icc)$")
    add_definitions (-fvisibility=hidden)
endif ()

add_library (nanomsg SHARED ${NN_SOURCES})
set_target_properties (nanomsg PROPERTIES VERSION "${NN_VERSION_STR}")

add_definitions (-DNN_EXPORTS)

if (UNIX AND NOT NN_HAVE_OSX)
    target_link_libraries (nanomsg rt)
endif ()

if (NN_HAVE_WINDOWS)
    target_link_libraries (nanomsg ws2_32)
    target_link_libraries (nanomsg Mswsock.lib)
    target_link_libraries (nanomsg Advapi32.lib)
endif ()

if (NN_HAVE_SOLARIS)
    target_link_libraries (nanomsg socket)
    target_link_libraries (nanomsg nsl)
endif ()

target_link_libraries (nanomsg ${CMAKE_THREAD_LIBS_INIT})

install (TARGETS nanomsg DESTINATION lib)

# pkg-config file
if (UNIX)
    configure_file (pkgconfig.in ${CMAKE_PROJECT_NAME}.pc @ONLY)
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.pc DESTINATION lib/pkgconfig)
endif ()

