/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.
    Copyright 2016 Garrett D'Amore <garrett@damore.org>
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

#include "bipc.h"
#include "cipc.h"
#include "ipc.h"

#include "../../ipc.h"
#include "../../unix.h"
#include "../../winpipe.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/cont.h"

#include <string.h>
#if defined NN_HAVE_WINDOWS
#include "../../utils/win.h"
/*  This reparse tag is absent from older Windows SDKs. */
#ifndef IO_REPARSE_TAG_AF_UNIX
#define IO_REPARSE_TAG_AF_UNIX 0x80000023
#endif
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

/*  IPC-specific socket options. */
struct nn_ipc_optset {
    struct nn_optset base;
    
    /* Win32 Security Attribute */
    void* sec_attr;

    int outbuffersz;
    int inbuffersz;
};

static void nn_ipc_optset_destroy (struct nn_optset *self);
static int nn_ipc_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen);
static int nn_ipc_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen);
static const struct nn_optset_vfptr nn_ipc_optset_vfptr = {
    nn_ipc_optset_destroy,
    nn_ipc_optset_setopt,
    nn_ipc_optset_getopt
};

/*  Both transports share the historical IPC message framing. */
static int nn_unix_bind (struct nn_ep *ep);
static int nn_unix_connect (struct nn_ep *ep);
static struct nn_optset *nn_ipc_optset (void);

struct nn_transport nn_unix = {
    "unix", NN_UNIX, NULL, NULL, nn_unix_bind, nn_unix_connect,
#if defined NN_HAVE_WINDOWS
    NULL
#else
    nn_ipc_optset
#endif
};

static int nn_unix_bind (struct nn_ep *ep)
{
    return nn_bipc_create (ep, AF_UNIX);
}

static int nn_unix_connect (struct nn_ep *ep)
{
    return nn_cipc_create (ep, AF_UNIX);
}

#if defined NN_HAVE_WINDOWS
static int nn_winpipe_bind (struct nn_ep *ep)
{
    return nn_bipc_create (ep, NN_USOCK_WINPIPE);
}

static int nn_winpipe_connect (struct nn_ep *ep)
{
    return nn_cipc_create (ep, NN_USOCK_WINPIPE);
}

struct nn_transport nn_winpipe = {
    "winpipe", NN_WINPIPE, NULL, NULL,
    nn_winpipe_bind, nn_winpipe_connect, nn_ipc_optset
};
#endif

int nn_ipc_resolve (const char *addr, int domain, struct sockaddr_storage *ss)
{
    struct sockaddr_un *un;
    size_t len;
#if defined NN_HAVE_WINDOWS
    struct nn_sockaddr_winpipe *pipe;
#endif

    len = strlen (addr);
    if (!len)
        return -EINVAL;
    memset (ss, 0, sizeof (*ss));
#if defined NN_HAVE_WINDOWS
    if (domain == NN_USOCK_WINPIPE) {
        pipe = (struct nn_sockaddr_winpipe *) ss;
        if (len >= sizeof (pipe->sun_path))
            return -ENAMETOOLONG;
        if (strchr (addr, '\\'))
            return -EINVAL;
        memcpy (pipe->sun_path, addr, len + 1);
        return sizeof (*pipe);
    }
#else
    (void) domain;
#endif
    un = (struct sockaddr_un *) ss;
    if (len >= sizeof (un->sun_path))
        return -ENAMETOOLONG;
    un->sun_family = AF_UNIX;
    memcpy (un->sun_path, addr, len + 1);
    return sizeof (*un);
}

void nn_ipc_unlink (const char *addr)
{
#if defined NN_HAVE_WINDOWS
    WCHAR path [108];

    /*  Winsock AF_UNIX paths are UTF-8, regardless of the ANSI code page. */
    if (MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, addr, -1,
          path, sizeof (path) / sizeof (path [0])))
        DeleteFileW (path);
#else
    int rc;

    rc = unlink (addr);
    errno_assert (rc == 0 || errno == ENOENT);
#endif
}

void nn_ipc_file_init (struct nn_ipc_file *self)
{
#if defined NN_HAVE_WINDOWS
    self->handle = INVALID_HANDLE_VALUE;
#else
    self->owned = 0;
#endif
}

void nn_ipc_file_capture (struct nn_ipc_file *self, const char *addr)
{
#if defined NN_HAVE_WINDOWS
    WCHAR path [108];
    FILE_ATTRIBUTE_TAG_INFO info;

    if (!MultiByteToWideChar (CP_UTF8, MB_ERR_INVALID_CHARS, addr, -1,
          path, sizeof (path) / sizeof (path [0])))
        return;

    /*  Retain the reparse point itself, rather than following it. Deleting
        through this handle cannot remove a replacement at the old name. */
    self->handle = CreateFileW (path, DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (self->handle == INVALID_HANDLE_VALUE)
        return;

    if (!GetFileInformationByHandleEx (self->handle, FileAttributeTagInfo,
          &info, sizeof (info)) || info.ReparseTag != IO_REPARSE_TAG_AF_UNIX) {
        CloseHandle (self->handle);
        self->handle = INVALID_HANDLE_VALUE;
    }
#else
    struct stat st;

    if (lstat (addr, &st) == 0 && S_ISSOCK (st.st_mode)) {
        self->dev = st.st_dev;
        self->ino = st.st_ino;
        self->owned = 1;
    }
#endif
}

void nn_ipc_file_unlink (struct nn_ipc_file *self, const char *addr)
{
#if defined NN_HAVE_WINDOWS
    FILE_DISPOSITION_INFO info;

    (void) addr;
    if (self->handle == INVALID_HANDLE_VALUE)
        return;
    info.DeleteFile = TRUE;
    SetFileInformationByHandle (self->handle, FileDispositionInfo,
        &info, sizeof (info));
    nn_ipc_file_term (self);
#else
    struct stat st;

    if (!self->owned)
        return;
    if (lstat (addr, &st) == 0 && S_ISSOCK (st.st_mode) &&
          st.st_dev == self->dev && st.st_ino == self->ino)
        nn_ipc_unlink (addr);
    self->owned = 0;
#endif
}

void nn_ipc_file_term (struct nn_ipc_file *self)
{
#if defined NN_HAVE_WINDOWS
    if (self->handle != INVALID_HANDLE_VALUE)
        CloseHandle (self->handle);
#endif
    nn_ipc_file_init (self);
}

static struct nn_optset *nn_ipc_optset ()
{
    struct nn_ipc_optset *optset;

    optset = nn_alloc (sizeof (struct nn_ipc_optset), "optset (ipc)");
    alloc_assert (optset);
    optset->base.vfptr = &nn_ipc_optset_vfptr;

    /*  Default values for the IPC options */
    optset->sec_attr = NULL;
    optset->outbuffersz = 4096;
    optset->inbuffersz = 4096;

    return &optset->base;   
}

static void nn_ipc_optset_destroy (struct nn_optset *self)
{
    struct nn_ipc_optset *optset;

    optset = nn_cont (self, struct nn_ipc_optset, base);
    nn_free (optset);
}

static int nn_ipc_optset_setopt (struct nn_optset *self, int option,
    const void *optval, size_t optvallen)
{
    struct nn_ipc_optset *optset;

    optset = nn_cont (self, struct nn_ipc_optset, base);
    if (optvallen < sizeof (int)) {
        return -EINVAL;
    }

    switch (option) {
    case NN_IPC_SEC_ATTR: 
        optset->sec_attr = (void *)optval;
        return 0;
    case NN_IPC_OUTBUFSZ:
        optset->outbuffersz = *(int *)optval;
        return 0;
    case NN_IPC_INBUFSZ:
        optset->inbuffersz = *(int *)optval;
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

static int nn_ipc_optset_getopt (struct nn_optset *self, int option,
    void *optval, size_t *optvallen)
{
    struct nn_ipc_optset *optset;

    optset = nn_cont (self, struct nn_ipc_optset, base);

    switch (option) {
    case NN_IPC_SEC_ATTR: 
        memcpy(optval, &optset->sec_attr, sizeof(optset->sec_attr));
        *optvallen = sizeof(optset->sec_attr);
        return 0;
    case NN_IPC_OUTBUFSZ:
        *(int *)optval = optset->outbuffersz;
        *optvallen = sizeof (int);
        return 0;
    case NN_IPC_INBUFSZ:
        *(int *)optval = optset->inbuffersz;
        *optvallen = sizeof (int);
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}
