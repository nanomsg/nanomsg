Welcome to nanomsg
==================

[![Release](https://img.shields.io/github/release/nanomsg/nanomsg.svg)](https://github.com/nanomsg/nanomsg/releases/latest)
[![MIT License](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/nanomsg/nanomsg/blob/master/COPYING)
[![Linux Status](https://img.shields.io/travis/nanomsg/nanomsg/master.svg?label=linux)](https://travis-ci.org/nanomsg/nanomsg)
[![Windows Status](https://img.shields.io/appveyor/ci/nanomsg/nanomsg/master.svg?label=windows)](https://ci.appveyor.com/project/nanomsg/nanomsg)
[![Gitter](https://img.shields.io/badge/gitter-join-brightgreen.svg)](https://gitter.im/nanomsg/nanomsg)

The nanomsg library is a simple high-performance implementation of several
"scalability protocols". These scalability protocols are light-weight messaging
protocols which can be used to solve a number of very common messaging
patterns, such as request/reply, publish/subscribe, surveyor/respondent,
and so forth.  These protocols can run over a variety of transports such
as TCP, UNIX sockets, and even WebSocket.

For more information check the [website](http://nanomsg.org).

Prerequisites
-------------

1. Windows.
   * Windows Vista or newer (Windows XP and 2003 are *NOT* supported)
   * Microsoft Visual Studio 2010 (including C++) or newer, or mingw-w64
     (Specifically mingw and older Microsoft compilers are *NOT supported)
   * CMake 2.8.7 or newer, available in $PATH as `cmake`

2. POSIX (Linux, MacOS X, UNIX)
   * ANSI C compiler supporting C89
   * POSIX pthreads (should be present on all modern POSIX systems)
   * BSD sockets support for both TCP and UNIX domain sockets
   * CMake (http://cmake.org) 2.8.7 or newer, available in $PATH as `cmake`

3. Documentation (optional)
   * asciidoctor (http://asciidoctor.org/) available as `asciidoctor`
   * If not present, docs are not formatted, but left in readable ASCII
   * Also available on-line at http://nanomsg.org/documentation

Build it with CMake
-------------------

(This assumes you want to use the default generator.  To use a
different generator specify `-G <generator>` to the cmake command line.
The list of generators supported can be obtained using `cmake --help`.)

1.  Go to the root directory of the local source repository.
2.  To perform an out-of-source build, run:
3.  `mkdir build`
4.  `cd build`
5.  `cmake ..`
    (You can add `-DCMAKE_INSTALL_PREFIX=/usr/local` or some other directory.
    You can specify a release build with `-DCMAKE_BUILD_TYPE=Release`.
6.  `cmake --build .`
7.  `ctest -C Debug .`
8.  `cmake --build . --target install`
    *NB:* This may have to be done as a privileged user.
9.  (Linux only).  `ldconfig` (As a privileged or root user.)

Static Library (Windows Only)
-----------------------------

We normally build a dynamic library (.DLL) by default.

If you want a static library (.LIB) , configure by passing `-DNN_STATIC_LIB=ON`
to the `cmake` command.

You will also need to define `NN_STATIC_LIB` in your compilation environment
when building programs that use this library.

This is required because of the way Windows DLL exports happen.  This is not
necessary when compiling on POSIX platforms.

Resources
---------

Website: [http://nanomsg.org](http://nanomsg.org)

Source code: [https://github.com/nanomsg/nanomsg](http://github.com/nanomsg/nanomsg)

Documentation: [http://nanomsg.org/documentation.html](http://nanomsg.org/documentation.html)

Bug tracker: [https://github.com/nanomsg/nanomsg/issues](http://github.com/nanomsg/nanomsg/issues)

Mailing list: [nanomsg@freelists.org](http://www.freelists.org/list/nanomsg)

Gitter Chat: [https://gitter.im/nanomsg/nanomsg](https://gitter.im/nanomsg/nanomsg)

IRC chatroom: [#nanomsg at irc.freenode.net/8001](http://webchat.freenode.net?channels=%23nanomsg)
