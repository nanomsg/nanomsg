/*
    Copyright (c) 2012 Martin Sustrik  All rights reserved.
    Copyright 2017 Garrett D'Amore <garrett@damore.org>
    Copyright 2026 Staysail Systems, Inc.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#include "../src/nn.h"
#include "../src/pair.h"
#include "../src/ipc.h"
#include "../src/unix.h"
#include "testutil.h"

#if defined NN_HAVE_WINDOWS
#include "../src/utils/win.h"
typedef SOCKET raw_socket;
#define raw_close closesocket
#define RAW_INVALID INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
typedef int raw_socket;
#define raw_close close
#define RAW_INVALID -1
#endif

#define ADDRESS "unix://test.unix"
#define PATH "test.unix"

static void check_removed (const char *path)
{
#if defined NN_HAVE_WINDOWS
    WCHAR wide [108];
    nn_assert (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
        wide, sizeof (wide) / sizeof (wide [0])) > 0);
    nn_assert (GetFileAttributesW (wide) == INVALID_FILE_ATTRIBUTES);
    nn_assert (GetLastError () == ERROR_FILE_NOT_FOUND);
#else
    struct stat st;
    nn_assert (stat (path, &st) < 0);
    nn_assert (errno == ENOENT);
#endif
}

static void roundtrip (char *bindaddr, char *connectaddr)
{
    int sb;
    int sc;
    int timeout = 2000;
    char large [10001];

    sc = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (sc, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sc, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_connect (sc, connectaddr);
    nn_sleep (100);
    sb = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (sb, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sb, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_bind (sb, bindaddr);
    test_send (sc, "request");
    test_recv (sb, "request");
    memset (large, 'x', sizeof (large) - 1);
    large [sizeof (large) - 1] = 0;
    test_send (sb, large);
    test_recv (sc, large);
    test_close (sc);
    test_close (sb);
}

/*  A raw AF_UNIX peer proves this uses sockets, with the existing IPC wire
    format, on Windows as well as POSIX. */
static void raw_peer (void)
{
    int sb;
    int rc;
    int timeout = 2000;
    raw_socket raw;
    struct sockaddr_un addr;
    uint8_t header [8];
    uint8_t message [12];

    sb = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (sb, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sb, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_bind (sb, ADDRESS);
    raw = socket (AF_UNIX, SOCK_STREAM, 0);
    nn_assert (raw != RAW_INVALID);
#if defined NN_HAVE_WINDOWS
    {
        DWORD raw_timeout = 2000;
        rc = setsockopt (raw, SOL_SOCKET, SO_RCVTIMEO,
            (const char *) &raw_timeout, sizeof (raw_timeout));
        nn_assert (rc == 0);
    }
#else
    {
        struct timeval raw_timeout = {2, 0};
        rc = setsockopt (raw, SOL_SOCKET, SO_RCVTIMEO,
            &raw_timeout, sizeof (raw_timeout));
        nn_assert (rc == 0);
    }
#endif
    memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    strcpy (addr.sun_path, PATH);
    rc = connect (raw, (struct sockaddr *) &addr, sizeof (addr));
    nn_assert (rc == 0);
    memcpy (header, "\0SP\0\0\0\0\0", sizeof (header));
    header [4] = (uint8_t) (NN_PAIR >> 8);
    header [5] = (uint8_t) NN_PAIR;
    rc = (int) send (raw, (const char *) header, sizeof (header), 0);
    nn_assert (rc == sizeof (header));
    rc = (int) recv (raw, (char *) header, sizeof (header), MSG_WAITALL);
    nn_assert (rc == sizeof (header));
    nn_assert (memcmp (header, "\0SP\0", 4) == 0);
    nn_assert (((header [4] << 8) | header [5]) == NN_PAIR);
    memset (message, 0, sizeof (message));
    message [0] = 1;
    message [8] = 3;
    memcpy (message + 9, "raw", 3);
    rc = (int) send (raw, (const char *) message, sizeof (message), 0);
    nn_assert (rc == sizeof (message));
    test_recv (sb, "raw");
    test_send (sb, "ack");
    rc = (int) recv (raw, (char *) message, sizeof (message), MSG_WAITALL);
    nn_assert (rc == sizeof (message));
    nn_assert (memcmp (message, "\1\0\0\0\0\0\0\0\3ack", 12) == 0);
    nn_assert (raw_close (raw) == 0);
    test_close (sb);
    check_removed (PATH);
}

/*  A replacement endpoint at the same name must survive the old endpoint's
    shutdown. Moving the original file makes the interleaving deterministic. */
static void replacement_endpoint (void)
{
    int old;
    int replacement;
    int client;
    int timeout = 2000;

    old = test_socket (AF_SP, NN_PAIR);
    test_bind (old, ADDRESS);
#if defined NN_HAVE_WINDOWS
    nn_assert (MoveFileW (L"test.unix", L"test.retired.unix"));
#else
    nn_assert (rename (PATH, "test.retired.unix") == 0);
#endif
    replacement = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (replacement, NN_SOL_SOCKET, NN_RCVTIMEO,
        &timeout, sizeof (timeout));
    test_bind (replacement, ADDRESS);
    test_close (old);

    client = test_socket (AF_SP, NN_PAIR);
    test_setsockopt (client, NN_SOL_SOCKET, NN_SNDTIMEO,
        &timeout, sizeof (timeout));
    test_connect (client, ADDRESS);
    test_send (client, "replacement");
    test_recv (replacement, "replacement");
    test_close (client);
    test_close (replacement);
    check_removed (PATH);
#if !defined NN_HAVE_WINDOWS
    /*  POSIX cleanup leaves the renamed file to the caller who moved it. */
    nn_assert (unlink ("test.retired.unix") == 0);
#endif
    check_removed ("test.retired.unix");
}

int main (void)
{
    int sb;
    int sc;
    int rc;
    raw_socket raw;
    struct sockaddr_un addr;
    char longaddr [160];
    int timeout = 2000;

    /*  Invalid addresses must return an error without aborting. */
    sc = test_socket (AF_SP, NN_PAIR);
    rc = nn_bind (sc, "unix://");
    nn_assert (rc < 0 && nn_errno () == EINVAL);
    rc = nn_connect (sc, "unix://");
    nn_assert (rc < 0 && nn_errno () == EINVAL);
    strcpy (longaddr, "unix://");
    memset (longaddr + 7, 'x', sizeof (addr.sun_path));
    longaddr [7 + sizeof (addr.sun_path)] = 0;
    rc = nn_bind (sc, longaddr);
    nn_assert (rc < 0 && nn_errno () == ENAMETOOLONG);
    rc = nn_connect (sc, longaddr);
    nn_assert (rc < 0 && nn_errno () == ENAMETOOLONG);
    test_connect (sc, ADDRESS);
    test_close (sc);

    roundtrip (ADDRESS, ADDRESS);
    check_removed (PATH);
#if !defined NN_HAVE_WINDOWS
    nn_assert (NN_IPC == NN_UNIX);
    roundtrip (ADDRESS, "ipc://test.unix");
    check_removed (PATH);
    roundtrip ("ipc://test.unix", ADDRESS);
    check_removed (PATH);
    sc = test_socket (AF_SP, NN_PAIR);
    rc = nn_bind (sc, "winpipe://test.unix");
    nn_assert (rc < 0 && nn_errno () == EPROTONOSUPPORT);
    test_close (sc);
#endif

    /*  Duplicate bind must preserve the original listener and its file. */
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, ADDRESS);
    sc = test_socket (AF_SP, NN_PAIR);
    rc = nn_bind (sc, ADDRESS);
    nn_assert (rc < 0 && nn_errno () == EADDRINUSE);
    test_setsockopt (sc, NN_SOL_SOCKET, NN_RCVTIMEO, &timeout, sizeof (timeout));
    test_setsockopt (sc, NN_SOL_SOCKET, NN_SNDTIMEO, &timeout, sizeof (timeout));
    test_connect (sc, ADDRESS);
    test_send (sc, "original");
    test_recv (sb, "original");
    test_close (sc);
    test_close (sb);
    check_removed (PATH);

    /*  Closing without a peer must remove the socket file too. */
    sb = test_socket (AF_SP, NN_PAIR);
    test_bind (sb, ADDRESS);
    test_close (sb);
    check_removed (PATH);

    /*  Recover a socket file left behind by a previous process. */
    sc = test_socket (AF_SP, NN_PAIR);
    raw = socket (AF_UNIX, SOCK_STREAM, 0);
    nn_assert (raw != RAW_INVALID);
    memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    strcpy (addr.sun_path, PATH);
    nn_assert (bind (raw, (struct sockaddr *) &addr, sizeof (addr)) == 0);
    nn_assert (raw_close (raw) == 0);
    test_bind (sc, ADDRESS);
    test_close (sc);
    check_removed (PATH);

    /*  Exercise UTF-8 paths and cleanup, including on Windows. */
    roundtrip ("unix://test-\xc3\xa9.unix", "unix://test-\xc3\xa9.unix");
    check_removed ("test-\xc3\xa9.unix");
    raw_peer ();
    replacement_endpoint ();
    return 0;
}
