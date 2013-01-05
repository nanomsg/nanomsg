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

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_library (nanomsg SHARED
    sp.h
    transport.h
    pattern.h
    inproc.h
    ipc.h
    tcp.h
    pair.h
    pubsub.h
    reqrep.h
    fanin.h
    fanout.h
    survey.h

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
    utils/aio_posix.inc
    utils/aio_win.inc
    utils/alloc.h
    utils/alloc.c
    utils/astream.h
    utils/astream.c
    utils/bstream.h
    utils/bstream.c
    utils/clock.h
    utils/clock.c
    utils/cond.h
    utils/cond.c
    utils/cont.h
    utils/cstream.h
    utils/cstream.c
    utils/efd.h
    utils/efd.c
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
    utils/poller_epoll.inc
    utils/poller_kqueue.inc
    utils/poller_poll.inc
    utils/queue.h
    utils/queue.c
    utils/random.h
    utils/random.c
    utils/sleep.h
    utils/sleep.c
    utils/stream.h
    utils/stream.c
    utils/thread.h
    utils/thread.c
    utils/timeout.h
    utils/timeout.c
    utils/wire.h
    utils/wire.c

    patterns/fanin/sink.h
    patterns/fanin/sink.c
    patterns/fanin/source.h
    patterns/fanin/source.c
    patterns/fanin/xsink.h
    patterns/fanin/xsink.c
    patterns/fanin/xsource.h
    patterns/fanin/xsource.c

    patterns/fanout/push.h
    patterns/fanout/push.c
    patterns/fanout/pull.h
    patterns/fanout/pull.c
    patterns/fanout/xpull.h
    patterns/fanout/xpull.c
    patterns/fanout/xpush.h
    patterns/fanout/xpush.c

    patterns/pair/pair.h
    patterns/pair/pair.c
    patterns/pair/xpair.h
    patterns/pair/xpair.c

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

    patterns/survey/respondent.h
    patterns/survey/respondent.c
    patterns/survey/surveyor.h
    patterns/survey/surveyor.c
    patterns/survey/xrespondent.h
    patterns/survey/xrespondent.c
    patterns/survey/xsurveyor.h
    patterns/survey/xsurveyor.c

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

    transports/ipc/ipc.h
    transports/ipc/ipc.c

    transports/tcp/tcp.h
    transports/tcp/tcp.c
    )

add_definitions (-DSP_EXPORTS)

if (UNIX AND NOT SP_HAVE_OSX)
    target_link_libraries (nanomsg rt)
endif ()

if (SP_HAVE_WINDOWS)
    target_link_libraries (nanomsg ws2_32)
    target_link_libraries (nanomsg Mswsock.lib)
endif ()

if (SP_HAVE_SOLARIS)
    target_link_libraries (nanomsg socket)
    target_link_libraries (nanomsg nsl)
endif ()

target_link_libraries (nanomsg ${CMAKE_THREAD_LIBS_INIT})

install (TARGETS nanomsg DESTINATION lib)

