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

#  On Windows there are no tools to build the documentation.
if (NOT WIN32)

    #  Documentation topics to generate.
    set (topics

        #  Main page.
        nanomsg.7

        #  Protocols.
        nn_pair.7
        nn_reqrep.7
        nn_pubsub.7
        nn_survey.7
        nn_fanin.7
        nn_fanout.7

        #  Transports.
        nn_inproc.7
        nn_ipc.7
        nn_tcp.7

        #  Functions.
        nn_version.3
        nn_errno.3
        nn_strerror.3
        nn_term.3
        nn_allocmsg.3
        nn_freemsg.3
        nn_socket.3
        nn_close.3
        nn_getsockopt.3
        nn_setsockopt.3
        nn_bind.3
        nn_connect.3
        nn_shutdown.3
        nn_send.3
        nn_recv.3
        nn_sendmsg.3
        nn_recvmsg.3

        #  Macros.
        nn_cmsg.3

        #  Compatibility.
        nn_zmq.7

        )

    #  Build the genconf program.
    add_executable (genconf genconf.c)

    #  Generate asciidoc.conf file.
    add_custom_command (
        DEPENDS genconf
        COMMAND genconf
        OUTPUT asciidoc.conf)

    #  Generate the rules to build the documentation.
    #  During the process collect the names of resulting documentation files.
    set (docs)
    foreach (topic ${topics})

        #  Generate intermediate docbook file for the manpage.
        add_custom_command (
            DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/${topic}.txt
                asciidoc.conf
            COMMAND asciidoc -d manpage -b docbook
                -f asciidoc.conf -o ${topic}.xml
                ${CMAKE_CURRENT_SOURCE_DIR}/${topic}.txt
            OUTPUT ${topic}.xml)

        #  Generate the manpage.
        add_custom_command (
            DEPENDS ${topic}.xml
            COMMAND xmlto -o ${CMAKE_BINARY_DIR} man ${topic}.xml
            OUTPUT ${CMAKE_BINARY_DIR}/${topic})
        set (docs ${docs} ${CMAKE_BINARY_DIR}/${topic})
        get_filename_component (category ${topic} EXT)
        string (REGEX REPLACE "^\\." "" category "${category}")
        install (FILES "${CMAKE_BINARY_DIR}/${topic}"
            DESTINATION share/man/man${category} OPTIONAL)

        #  Generate the HTML page.
        add_custom_command (
            DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/${topic}.txt
                asciidoc.conf
                ${CMAKE_CURRENT_SOURCE_DIR}/htmldoc.css
            COMMAND asciidoc -d manpage -b xhtml11
                -a stylesheet=${CMAKE_CURRENT_SOURCE_DIR}/htmldoc.css
                -f asciidoc.conf
                -o ${CMAKE_BINARY_DIR}/${topic}.html
                ${CMAKE_CURRENT_SOURCE_DIR}/${topic}.txt
            OUTPUT ${CMAKE_BINARY_DIR}/${topic}.html)
        set (docs ${docs} ${CMAKE_BINARY_DIR}/${topic}.html)

    endforeach (topic)

    # 'doc' target builds the documentation.
    add_custom_target(doc DEPENDS ${docs})

endif ()

