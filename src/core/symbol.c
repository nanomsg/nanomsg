/*
    Copyright (c) 2013 Evan Wies <evan@neomantra.net>
    Copyright (c) 2013 GoPivotal, Inc.  All rights reserved.

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

#include "../nn.h"

#include "../inproc.h"
#include "../ipc.h"
#include "../tcp.h"

#include "../pair.h"
#include "../pubsub.h"
#include "../reqrep.h"
#include "../pipeline.h"
#include "../survey.h"
#include "../bus.h"

struct sym_value_name {
    int value;
    const char* name;
};

static const struct sym_value_name sym_value_names [] = {

    {NN_VERSION_CURRENT, "NN_VERSION_CURRENT"},
    {NN_VERSION_REVISION, "NN_VERSION_REVISION"},
    {NN_VERSION_AGE, "NN_VERSION_AGE"},

    {AF_SP, "AF_SP"},
    {AF_SP_RAW, "AF_SP_RAW"},

    {NN_INPROC, "NN_INPROC"},
    {NN_IPC, "NN_IPC"},
    {NN_TCP, "NN_TCP"},

    {NN_PAIR, "NN_PAIR"},
    {NN_PUB, "NN_PUB"},
    {NN_SUB, "NN_SUB"},
    {NN_REP, "NN_REP"},
    {NN_REQ, "NN_REQ"},
    {NN_PUSH, "NN_PUSH"},
    {NN_PULL, "NN_PULL"},
    {NN_SURVEYOR, "NN_SURVEYOR"},
    {NN_RESPONDENT, "NN_RESPONDENT"},
    {NN_BUS, "NN_BUS"},

    {NN_SOCKADDR_MAX, "NN_SOCKADDR_MAX"},

    {NN_SOL_SOCKET, "NN_SOL_SOCKET"},

    {NN_LINGER, "NN_LINGER"},
    {NN_SNDBUF, "NN_SNDBUF"},
    {NN_RCVBUF, "NN_RCVBUF"},
    {NN_SNDTIMEO, "NN_SNDTIMEO"},
    {NN_RCVTIMEO, "NN_RCVTIMEO"},
    {NN_RECONNECT_IVL, "NN_RECONNECT_IVL"},
    {NN_RECONNECT_IVL_MAX, "NN_RECONNECT_IVL_MAX"},
    {NN_SNDPRIO, "NN_SNDPRIO"},
    {NN_SNDFD, "NN_SNDFD"},
    {NN_RCVFD, "NN_RCVFD"},
    {NN_DOMAIN, "NN_DOMAIN"},
    {NN_PROTOCOL, "NN_PROTOCOL"},

    {NN_SUB_SUBSCRIBE, "NN_SUB_SUBSCRIBE"},
    {NN_SUB_UNSUBSCRIBE, "NN_SUB_UNSUBSCRIBE"},
    {NN_REQ_RESEND_IVL, "NN_REQ_RESEND_IVL"},
    {NN_SURVEYOR_DEADLINE, "NN_SURVEYOR_DEADLINE"},
    {NN_TCP_NODELAY, "NN_TCP_NODELAY"},

    {NN_DONTWAIT, "NN_DONTWAIT"},

    {EADDRINUSE, "EADDRINUSE"},
    {EADDRNOTAVAIL, "EADDRNOTAVAIL"},
    {EAFNOSUPPORT, "EAFNOSUPPORT"},
    {EAGAIN, "EAGAIN"},
    {EBADF, "EBADF"},
    {ECONNREFUSED, "ECONNREFUSED"},
    {EFAULT, "EFAULT"},
    {EFSM, "EFSM"},
    {EINPROGRESS, "EINPROGRESS"},
    {EINTR, "EINTR"},
    {EINVAL, "EINVAL"},
    {EMFILE, "EMFILE"},
    {ENAMETOOLONG, "ENAMETOOLONG"},
    {ENETDOWN, "ENETDOWN"},
    {ENOBUFS, "ENOBUFS"},
    {ENODEV, "ENODEV"},
    {ENOMEM, "ENOMEM"},
    {ENOPROTOOPT, "ENOPROTOOPT"},
    {ENOTSOCK, "ENOTSOCK"},
    {ENOTSUP, "ENOTSUP"},
    {EPROTO, "EPROTO"},
    { EPROTONOSUPPORT, "EPROTONOSUPPORT"},
    {ETERM, "ETERM"},
    {ETIMEDOUT, "ETIMEDOUT"},
#ifdef NN_HAVE_WINDOWS
    {EACCES, "EACCES" },
    {ECONNABORTED, "ECONNABORTED"},
    {ECONNRESET, "ECONNRESET"},
    {EHOSTUNREACH, "EHOSTUNREACH"},
    {EMSGSIZE, "EMSGSIZE"},
    {ENETRESET, "ENETRESET"},
    {ENETUNREACH, "ENETUNREACH"},
    {ENOTCONN, "ENOTCONN"},
#endif
};

const int SYM_VALUE_NAMES_LEN = (sizeof (sym_value_names) /
    sizeof (sym_value_names [0]));

const char *nn_symbol (int i, int *value)
{
    const struct sym_value_name *svn;
    if (i < 0 || i >= SYM_VALUE_NAMES_LEN) {
        errno = EINVAL;
        return NULL;
    }

    svn = &sym_value_names [i];
    if (value)
        *value = svn->value;
    return svn->name;
}

