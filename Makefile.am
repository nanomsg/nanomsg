# Copyright (c) 2013 Luca Barbato
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

ACLOCAL_AMFLAGS = -I m4

include_HEADERS = \
    src/nn.h \
    src/protocol.h \
    src/inproc.h \
    src/ipc.h \
    src/tcp.h \
    src/pair.h \
    src/pubsub.h \
    src/reqrep.h \
    src/fanin.h \
    src/fanout.h \
    src/survey.h \
    src/bus.h

lib_LTLIBRARIES = libnanomsg.la

NANOMSG_CORE = \
    src/core/ep.h \
    src/core/ep.c \
    src/core/epbase.c \
    src/core/global.h \
    src/core/global.c \
    src/core/pipe.c \
    src/core/sock.h \
    src/core/sock.c \
    src/core/sockbase.c \
    src/core/device.c \
    src/core/symbol.c

NANOMSG_AIO = \
    src/aio/ctx.h \
    src/aio/ctx.c \
    src/aio/fsm.h \
    src/aio/fsm.c \
    src/aio/poller.h \
    src/aio/poller.c \
    src/aio/poller_epoll.h \
    src/aio/poller_epoll.inc \
    src/aio/poller_kqueue.h \
    src/aio/poller_kqueue.inc \
    src/aio/poller_poll.h \
    src/aio/poller_poll.inc \
    src/aio/pool.h \
    src/aio/pool.c \
    src/aio/timer.h \
    src/aio/timer.c \
    src/aio/timerset.h \
    src/aio/timerset.c \
    src/aio/usock.h \
    src/aio/usock.c \
    src/aio/usock_posix.h \
    src/aio/usock_posix.inc \
    src/aio/usock_win.h \
    src/aio/usock_win.inc \
    src/aio/worker.h \
    src/aio/worker.c \
    src/aio/worker_posix.h \
    src/aio/worker_posix.inc \
    src/aio/worker_win.h \
    src/aio/worker_win.inc

NANOMSG_UTILS = \
    src/utils/alloc.h \
    src/utils/alloc.c \
    src/utils/atomic.h \
    src/utils/atomic.c \
    src/utils/chunk.h \
    src/utils/chunk.c \
    src/utils/chunkref.h \
    src/utils/chunkref.c \
    src/utils/clock.h \
    src/utils/clock.c \
    src/utils/cont.h \
    src/utils/efd.h \
    src/utils/efd.c \
    src/utils/efd_eventfd.h \
    src/utils/efd_eventfd.inc \
    src/utils/efd_pipe.h \
    src/utils/efd_pipe.inc \
    src/utils/efd_socketpair.h \
    src/utils/efd_socketpair.inc \
    src/utils/efd_win.h \
    src/utils/efd_win.inc \
    src/utils/err.h \
    src/utils/err.c \
    src/utils/fast.h \
    src/utils/glock.h \
    src/utils/glock.c \
    src/utils/hash.h \
    src/utils/hash.c \
    src/utils/list.h \
    src/utils/list.c \
    src/utils/msg.h \
    src/utils/msg.c \
    src/utils/mutex.h \
    src/utils/mutex.c \
    src/utils/queue.h \
    src/utils/queue.c \
    src/utils/random.h \
    src/utils/random.c \
    src/utils/sem.h \
    src/utils/sem.c \
    src/utils/sleep.h \
    src/utils/sleep.c \
    src/utils/thread.h \
    src/utils/thread.c \
    src/utils/thread_posix.h \
    src/utils/thread_posix.inc \
    src/utils/thread_win.h \
    src/utils/thread_win.inc \
    src/utils/wire.h \
    src/utils/wire.c

PROTOCOLS_UTILS = \
    src/protocols/utils/dist.h \
    src/protocols/utils/dist.c \
    src/protocols/utils/excl.h \
    src/protocols/utils/excl.c \
    src/protocols/utils/fq.h \
    src/protocols/utils/fq.c \
    src/protocols/utils/lb.h \
    src/protocols/utils/lb.c \
    src/protocols/utils/priolist.h \
    src/protocols/utils/priolist.c

PROTOCOLS_BUS = \
    src/protocols/bus/bus.h \
    src/protocols/bus/bus.c \
    src/protocols/bus/xbus.h \
    src/protocols/bus/xbus.c

PROTOCOLS_FANIN = \
    src/protocols/fanin/sink.h \
    src/protocols/fanin/sink.c \
    src/protocols/fanin/source.h \
    src/protocols/fanin/source.c \
    src/protocols/fanin/xsink.h \
    src/protocols/fanin/xsink.c \
    src/protocols/fanin/xsource.h \
    src/protocols/fanin/xsource.c

PROTOCOLS_FANOUT = \
    src/protocols/fanout/push.h \
    src/protocols/fanout/push.c \
    src/protocols/fanout/pull.h \
    src/protocols/fanout/pull.c \
    src/protocols/fanout/xpull.h \
    src/protocols/fanout/xpull.c \
    src/protocols/fanout/xpush.h \
    src/protocols/fanout/xpush.c

PROTOCOLS_PAIR = \
    src/protocols/pair/pair.h \
    src/protocols/pair/pair.c \
    src/protocols/pair/xpair.h \
    src/protocols/pair/xpair.c

PROTOCOLS_PUBSUB = \
    src/protocols/pubsub/pub.h \
    src/protocols/pubsub/pub.c \
    src/protocols/pubsub/sub.h \
    src/protocols/pubsub/sub.c \
    src/protocols/pubsub/trie.h \
    src/protocols/pubsub/trie.c

PROTOCOLS_REQREP = \
    src/protocols/reqrep/req.h \
    src/protocols/reqrep/req.c \
    src/protocols/reqrep/rep.h \
    src/protocols/reqrep/rep.c \
    src/protocols/reqrep/xrep.h \
    src/protocols/reqrep/xrep.c \
    src/protocols/reqrep/xreq.h \
    src/protocols/reqrep/xreq.c

PROTOCOLS_SURVEY = \
    src/protocols/survey/respondent.h \
    src/protocols/survey/respondent.c \
    src/protocols/survey/surveyor.h \
    src/protocols/survey/surveyor.c \
    src/protocols/survey/xrespondent.h \
    src/protocols/survey/xrespondent.c \
    src/protocols/survey/xsurveyor.h \
    src/protocols/survey/xsurveyor.c

NANOMSG_PROTOCOLS = \
    $(PROTOCOLS_BUS) \
    $(PROTOCOLS_FANIN) \
    $(PROTOCOLS_FANOUT) \
    $(PROTOCOLS_PAIR) \
    $(PROTOCOLS_PUBSUB) \
    $(PROTOCOLS_REQREP) \
    $(PROTOCOLS_SURVEY) \
    $(PROTOCOLS_UTILS) \
    $(PROTOCOLS_INPROC) \
    $(PROTOCOLS_IPC) \
    $(PROTOCOLS_TCP)


TRANSPORTS_UTILS = \
    src/transports/utils/backoff.h \
    src/transports/utils/backoff.c \
    src/transports/utils/dns.h \
    src/transports/utils/dns.c \
    src/transports/utils/dns_getaddrinfo.h \
    src/transports/utils/dns_getaddrinfo.inc \
    src/transports/utils/dns_getaddrinfo_a.h \
    src/transports/utils/dns_getaddrinfo_a.inc \
    src/transports/utils/iface.h \
    src/transports/utils/iface.c \
    src/transports/utils/literal.h \
    src/transports/utils/literal.c \
    src/transports/utils/port.h \
    src/transports/utils/port.c \
    src/transports/utils/streamhdr.h \
    src/transports/utils/streamhdr.c

TRANSPORTS_INPROC = \
    src/transports/inproc/binproc.h \
    src/transports/inproc/binproc.c \
    src/transports/inproc/cinproc.h \
    src/transports/inproc/cinproc.c \
    src/transports/inproc/inproc.h \
    src/transports/inproc/inproc.c \
    src/transports/inproc/msgqueue.h \
    src/transports/inproc/msgqueue.c \
    src/transports/inproc/sinproc.h \
    src/transports/inproc/sinproc.c

TRANSPORTS_IPC = \
    src/transports/ipc/aipc.h \
    src/transports/ipc/aipc.c \
    src/transports/ipc/bipc.h \
    src/transports/ipc/bipc.c \
    src/transports/ipc/cipc.h \
    src/transports/ipc/cipc.c \
    src/transports/ipc/ipc.h \
    src/transports/ipc/ipc.c \
    src/transports/ipc/sipc.h \
    src/transports/ipc/sipc.c

TRANSPORTS_TCP = \
    src/transports/tcp/atcp.h \
    src/transports/tcp/atcp.c \
    src/transports/tcp/btcp.h \
    src/transports/tcp/btcp.c \
    src/transports/tcp/ctcp.h \
    src/transports/tcp/ctcp.c \
    src/transports/tcp/stcp.h \
    src/transports/tcp/stcp.c \
    src/transports/tcp/tcp.h \
    src/transports/tcp/tcp.c

NANOMSG_TRANSPORTS = \
    $(TRANSPORTS_UTILS) \
    $(TRANSPORTS_INPROC) \
    $(TRANSPORTS_IPC) \
    $(TRANSPORTS_TCP)

libnanomsg_la_SOURCES = \
    src/transport.h \
    src/protocol.h \
    $(NANOMSG_CORE) \
    $(NANOMSG_AIO) \
    $(NANOMSG_UTILS) \
    $(NANOMSG_PROTOCOLS) \
    $(NANOMSG_TRANSPORTS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libnanomsg.pc

libnanomsg_la_LDFLAGS = -version-info @NN_LT_VERSION@

if COMPAT
lib_LTLIBRARIES += libzmq.la

libzmq_la_SOURCES = \
    compat/zmq/zmq.h \
    compat/zmq/zmq_utils.h \
    compat/zmq/zmq.c

pkgconfig_DATA += compat/zmq/libzmq.pc

libzmq_la_LDFLAGS = -version-info 1:1:0
endif

if DOC

docsrcdir   = $(srcdir)/doc
docbuilddir = $(builddir)/doc

PROTOCOLS_MAN = \
    $(docbuilddir)/nn_pair.7 \
    $(docbuilddir)/nn_reqrep.7 \
    $(docbuilddir)/nn_pubsub.7 \
    $(docbuilddir)/nn_survey.7 \
    $(docbuilddir)/nn_fanin.7 \
    $(docbuilddir)/nn_fanout.7 \
    $(docbuilddir)/nn_bus.7

TRANSPORTS_MAN = \
    $(docbuilddir)/nn_inproc.7 \
    $(docbuilddir)/nn_ipc.7 \
    $(docbuilddir)/nn_tcp.7

FUNCTIONS_MAN = \
    $(docbuilddir)/nn_errno.3 \
    $(docbuilddir)/nn_strerror.3 \
    $(docbuilddir)/nn_symbol.3 \
    $(docbuilddir)/nn_term.3 \
    $(docbuilddir)/nn_allocmsg.3 \
    $(docbuilddir)/nn_freemsg.3 \
    $(docbuilddir)/nn_socket.3 \
    $(docbuilddir)/nn_close.3 \
    $(docbuilddir)/nn_getsockopt.3 \
    $(docbuilddir)/nn_setsockopt.3 \
    $(docbuilddir)/nn_bind.3 \
    $(docbuilddir)/nn_connect.3 \
    $(docbuilddir)/nn_shutdown.3 \
    $(docbuilddir)/nn_send.3 \
    $(docbuilddir)/nn_recv.3 \
    $(docbuilddir)/nn_sendmsg.3 \
    $(docbuilddir)/nn_recvmsg.3 \
    $(docbuilddir)/nn_device.3

man_MANS = \
    $(docbuilddir)/nanomsg.7 \
    $(PROTOCOLS_MAN) \
    $(TRANSPORTS_MAN) \
    $(FUNCTIONS_MAN) \
    $(docbuilddir)/nn_cmsg.3 \
    $(docbuilddir)/nn_zmq.7

asciidocconf = -d manpage -b docbook -f $(docsrcdir)/asciidoc.conf

SUFFIXES = .7.xml .3.xml .3 .7

.txt.xml:
	$(AM_V_GEN)$(ASCIIDOC) $(asciidocconf) -o $@ $<

.7.xml.7:
	$(AM_V_GEN)$(XMLTO) -o $(docbuilddir) man $<

.3.xml.3:
	$(AM_V_GEN)$(XMLTO) -o $(docbuilddir) man $<

CLEANFILES = $(man_MANS)

endif

bin_PROGRAMS = \
    perf/inproc_lat \
    perf/inproc_thr \
    perf/local_lat \
    perf/remote_lat \
    perf/local_thr \
    perf/remote_thr

LDADD = libnanomsg.la

TRANSPORT_TESTS = \
    tests/inproc \
    tests/inproc_shutdown \
    tests/ipc \
    tests/ipc_shutdown \
    tests/tcp \
    tests/tcp_shutdown

PROTOCOL_TESTS = \
    tests/pair \
    tests/pubsub \
    tests/reqrep \
    tests/fanin \
    tests/fanout \
    tests/survey \
    tests/bus

FEATURE_TESTS = \
    tests/block \
    tests/shutdown \
    tests/timeo \
    tests/iovec \
    tests/msg \
    tests/prio \
    tests/poll \
    tests/device \
    tests/emfile \
    tests/domain \
    tests/trie \
    tests/list \
    tests/hash \
    tests/symbol \
    tests/separation

check_PROGRAMS = \
    $(TRANSPORT_TESTS) \
    $(PROTOCOL_TESTS) \
    $(FEATURE_TESTS)

TESTS = $(check_PROGRAMS)


.version: version.sh .git
	./$< > $@

EXTRA_DIST = .version version.sh
