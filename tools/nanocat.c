/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

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

#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>
#include <nanomsg/pipeline.h>
#include <nanomsg/bus.h>
#include <nanomsg/pair.h>
#include <nanomsg/survey.h>
#include <nanomsg/reqrep.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include "options.h"

enum echo_format {
    NC_NO_ECHO,
    NC_ECHO_RAW,
    NC_ECHO_ASCII,
    NC_ECHO_QUOTED,
    NC_ECHO_MSGPACK
};

typedef struct nc_options {
    /* Global options */
    int verbose;

    /* Socket options */
    int socket_type;
    struct nc_string_list bind_addresses;
    struct nc_string_list connect_addresses;
    float send_timeout;
    float recv_timeout;
    struct nc_string_list subscriptions;

    /* Output options */
    float send_interval;
    struct nc_blob data_to_send;

    /* Input options */
    enum echo_format echo_format;
} nc_options_t;

/*  Constants to get address of in option declaration  */
static const int nn_push = NN_PUSH;
static const int nn_pull = NN_PULL;
static const int nn_pub = NN_PUB;
static const int nn_sub = NN_SUB;
static const int nn_req = NN_REQ;
static const int nn_rep = NN_REP;
static const int nn_bus = NN_BUS;
static const int nn_pair = NN_PAIR;
static const int nn_surveyor = NN_SURVEYOR;
static const int nn_respondent = NN_RESPONDENT;


struct nc_enum_item socket_types[] = {
    {"PUSH", NN_PUSH},
    {"PULL", NN_PULL},
    {"PUB", NN_PUB},
    {"SUB", NN_SUB},
    {"REQ", NN_REQ},
    {"REP", NN_REP},
    {"BUS", NN_BUS},
    {"PAIR", NN_PAIR},
    {"SURVEYOR", NN_SURVEYOR},
    {"RESPONDENT", NN_RESPONDENT},
    {NULL, 0},
};


/*  Constants to get address of in option declaration  */
static const int nc_echo_raw = NC_ECHO_RAW;
static const int nc_echo_ascii = NC_ECHO_ASCII;
static const int nc_echo_quoted = NC_ECHO_QUOTED;
static const int nc_echo_msgpack = NC_ECHO_MSGPACK;

struct nc_enum_item echo_formats[] = {
    {"no", NC_NO_ECHO},
    {"raw", NC_ECHO_RAW},
    {"ascii", NC_ECHO_ASCII},
    {"quoted", NC_ECHO_QUOTED},
    {"msgpack", NC_ECHO_MSGPACK},
    {NULL, 0},
};

/*  Constants for conflict masks  */
#define NC_MASK_SOCK 1
#define NC_MASK_WRITEABLE 2
#define NC_MASK_READABLE 4
#define NC_MASK_SOCK_SUB 8
#define NC_MASK_DATA 16
#define NC_MASK_ENDPOINT 32
#define NC_NO_PROVIDES 0
#define NC_NO_CONFLICTS 0
#define NC_NO_REQUIRES 0
#define NC_MASK_SOCK_WRITEABLE (NC_MASK_SOCK | NC_MASK_WRITEABLE)
#define NC_MASK_SOCK_READABLE (NC_MASK_SOCK | NC_MASK_READABLE)
#define NC_MASK_SOCK_READWRITE  (NC_MASK_SOCK_WRITEABLE|NC_MASK_SOCK_READABLE)

struct nc_option nc_options[] = {
    /* Generic options */
    {"verbose", 'v', NULL,
     NC_OPT_INCREMENT, offsetof (nc_options_t, verbose), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Generic", NULL, "Increase verbosity of the nanocat"},
    {"silent", 'q', NULL,
     NC_OPT_DECREMENT, offsetof (nc_options_t, verbose), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Generic", NULL, "Decrease verbosity of the nanocat"},
    {"help", 'h', NULL,
     NC_OPT_HELP, 0, NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Generic", NULL, "This help text"},

    /* Socket types */
    {"push", 0, "nn_push",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_push,
     NC_MASK_SOCK_WRITEABLE, NC_MASK_SOCK, NC_MASK_DATA,
     "Socket Types", NULL, "Use NN_PUSH socket type"},
    {"pull", 0, "nn_pull",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_pull,
     NC_MASK_SOCK_READABLE, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_PULL socket type"},
    {"pub", 0, "nn_pub",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_pub,
     NC_MASK_SOCK_WRITEABLE, NC_MASK_SOCK, NC_MASK_DATA,
     "Socket Types", NULL, "Use NN_PUB socket type"},
    {"sub", 0, "nn_sub",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_sub,
     NC_MASK_SOCK_READABLE|NC_MASK_SOCK_SUB, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_SUB socket type"},
    {"req", 0, "nn_req",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_req,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_MASK_DATA,
     "Socket Types", NULL, "Use NN_REQ socket type"},
    {"rep", 0, "nn_rep",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_rep,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_REP socket type"},
    {"surveyor", 0, "nn_surveyor",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_surveyor,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_MASK_DATA,
     "Socket Types", NULL, "Use NN_SURVEYOR socket type"},
    {"respondent", 0, "nn_respondent",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_respondent,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_RESPONDENT socket type"},
    {"bus", 0, "nn_bus",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_bus,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_BUS socket type"},
    {"pair", 0, "nn_pair",
     NC_OPT_SET_ENUM, offsetof (nc_options_t, socket_type), &nn_pair,
     NC_MASK_SOCK_READWRITE, NC_MASK_SOCK, NC_NO_REQUIRES,
     "Socket Types", NULL, "Use NN_PAIR socket type"},

    /* Socket Options */
    {"bind", 0, NULL,
     NC_OPT_LIST_APPEND, offsetof (nc_options_t, bind_addresses), NULL,
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "ADDR", "Bind socket to the address ADDR"},
    {"connect", 0, NULL,
     NC_OPT_LIST_APPEND, offsetof (nc_options_t, connect_addresses), NULL,
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "ADDR", "Connect socket to the address ADDR"},
    {"bind-ipc", 'X' , NULL, NC_OPT_LIST_APPEND_FMT,
     offsetof (nc_options_t, bind_addresses), "ipc://%s",
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "PATH", "Bind socket to the ipc address "
                               "\"ipc://PATH\"."},
    {"connect-ipc", 'x' , NULL, NC_OPT_LIST_APPEND_FMT,
     offsetof (nc_options_t, connect_addresses), "ipc://%s",
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "PATH", "Connect socket to the ipc address "
                               "\"ipc://PATH\"."},
    {"bind-local", 'L' , NULL, NC_OPT_LIST_APPEND_FMT,
     offsetof (nc_options_t, bind_addresses), "tcp://127.0.0.1:%s",
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "PORT", "Bind socket to the tcp address "
                               "\"tcp://127.0.0.1:PORT\"."},
    {"connect-local", 'l' , NULL, NC_OPT_LIST_APPEND_FMT,
     offsetof (nc_options_t, connect_addresses), "tcp://127.0.0.1:%s",
     NC_MASK_ENDPOINT, NC_NO_CONFLICTS, NC_NO_REQUIRES,
     "Socket Options", "PORT", "Connect socket to the tcp address "
                               "\"tcp://127.0.0.1:PORT\"."},
    {"recv-timeout", 0, NULL,
     NC_OPT_FLOAT, offsetof (nc_options_t, recv_timeout), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Socket Options", "SEC", "Set timeout for receiving a message"},
    {"send-timeout", 0, NULL,
     NC_OPT_FLOAT, offsetof (nc_options_t, send_timeout), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_WRITEABLE,
     "Socket Options", "SEC", "Set timeout for sending a message"},

    /* Pattern-specific options */
    {"subscribe", 0, NULL,
     NC_OPT_LIST_APPEND, offsetof (nc_options_t, subscriptions), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_SOCK_SUB,
     "SUB Socket Options", "PREFIX", "Subscribe to the prefix PREFIX. "
        "Note: socket will be subscribed to everything (empty prefix) if "
        "no prefixes are specified on the command-line."},

    /* Input Options */
    {"format", 0, NULL,
     NC_OPT_ENUM, offsetof (nc_options_t, echo_format), &echo_formats,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Input Options", "FORMAT", "Use echo format FORMAT "
                               "(same as the options below)"},
    {"raw", 0, NULL,
     NC_OPT_SET_ENUM, offsetof (nc_options_t, echo_format), &nc_echo_raw,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Input Options", NULL, "Dump message as is "
                           "(Note: no delimiters are printed)"},
    {"ascii", 'A', NULL,
     NC_OPT_SET_ENUM, offsetof (nc_options_t, echo_format), &nc_echo_ascii,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Input Options", NULL, "Print ASCII part of message delimited by newline. "
                           "All non-ascii characters replaced by dot."},
    {"quoted", 'Q', NULL,
     NC_OPT_SET_ENUM, offsetof (nc_options_t, echo_format), &nc_echo_quoted,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Input Options", NULL, "Print each message on separate line in double "
                           "quotes with C-like character escaping"},
    {"msgpack", 0, NULL,
     NC_OPT_SET_ENUM, offsetof (nc_options_t, echo_format), &nc_echo_msgpack,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_READABLE,
     "Input Options", NULL, "Print each message as msgpacked string (raw type)."
                           " This is useful for programmatic parsing."},

    /* Output Options */
    {"interval", 'i', NULL,
     NC_OPT_FLOAT, offsetof (nc_options_t, send_interval), NULL,
     NC_NO_PROVIDES, NC_NO_CONFLICTS, NC_MASK_WRITEABLE,
     "Output Options", "SEC", "Send message (or request) every SEC seconds"},
    {"data", 'D', NULL,
     NC_OPT_BLOB, offsetof (nc_options_t, data_to_send), &echo_formats,
     NC_MASK_DATA, NC_MASK_DATA, NC_MASK_WRITEABLE,
     "Output Options", "DATA", "Send DATA to the socket and quit for "
     "PUB, PUSH, PAIR, BUS socket. Use DATA to reply for REP or "
     " RESPONDENT socket. Send DATA as request for REQ or SURVEYOR socket."},
    {"file", 'F', NULL,
     NC_OPT_READ_FILE, offsetof (nc_options_t, data_to_send), &echo_formats,
     NC_MASK_DATA, NC_MASK_DATA, NC_MASK_WRITEABLE,
     "Output Options", "PATH", "Same as --data but get data from file PATH"},

    /* Sentinel */
    {NULL, 0, NULL,
     0, 0, NULL,
     0, 0, 0,
     NULL, NULL, NULL},
    };


struct nc_commandline nc_cli = {
    .short_description = "A command-line interface to nanomsg",
    .long_description = "",
    .options = nc_options,
    .required_options = NC_MASK_SOCK | NC_MASK_ENDPOINT,
    };


void nc_assert_errno (int flag, char *description)
{
    int err;

    if (!flag) {
        err = errno;
        fprintf (stderr, "%s: %s\n", description, nn_strerror (err));
        exit (3);
    }
}

void nc_sub_init (nc_options_t *options, int sock)
{
    int i;
    int rc;

    if (options->subscriptions.num) {
        for (i = 0; i < options->subscriptions.num; ++i) {
            rc = nn_setsockopt (sock, NN_SUB, NN_SUB_SUBSCRIBE,
                options->subscriptions.items[i],
                strlen (options->subscriptions.items[i]));
            nc_assert_errno (rc == 0, "Can't subscribe");
        }
    } else {
        rc = nn_setsockopt (sock, NN_SUB, NN_SUB_SUBSCRIBE, "", 0);
        nc_assert_errno (rc == 0, "Can't subscribe");
    }
}

void nc_set_recv_timeout (int sock, double timeo)
{
    int millis, rc;
    millis = (int)(timeo * 1000);
    rc = nn_setsockopt (sock, NN_SOL_SOCKET, NN_RCVTIMEO,
                       &millis, sizeof (millis));
    nc_assert_errno (rc == 0, "Can't set recv timeout");
}

int nc_create_socket (nc_options_t *options)
{
    int sock;
    int rc;
    int millis;

    sock = nn_socket (AF_SP, options->socket_type);
    nc_assert_errno (sock >= 0, "Can't create socket");

    /* Generic initialization */
    if (options->send_timeout >= 0) {
        millis = (int)(options->send_timeout * 1000);
        rc = nn_setsockopt (sock, NN_SOL_SOCKET, NN_SNDTIMEO,
                           &millis, sizeof (millis));
        nc_assert_errno (rc == 0, "Can't set send timeout");
    }
    if (options->recv_timeout >= 0) {
        nc_set_recv_timeout (sock, options->recv_timeout);
    }

    /* Specific intitalization */
    switch (options->socket_type) {
    case NN_SUB:
        nc_sub_init (options, sock);
        break;
    }

    return sock;
}

void nc_sleep (double seconds)
{
    struct timespec ts;
    int rc;

    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (seconds - (time_t)seconds)*1000000000;
    rc = nanosleep (&ts, NULL);
    nc_assert_errno (rc == 0, "Failed to sleep");
}

double nc_time ()
{
    struct timespec ts;
    int rc;

    rc = clock_gettime (CLOCK_MONOTONIC, &ts);
    nc_assert_errno (rc == 0, "Can't get current time");
    return ((double)ts.tv_sec) + ts.tv_nsec*0.000000001;
}

void nc_print_message (nc_options_t *options, char *buf, int buflen)
{
    switch (options->echo_format) {
    case NC_NO_ECHO:
        return;
    case NC_ECHO_RAW:
        fwrite (buf, 1, buflen, stdout);
        break;
    case NC_ECHO_ASCII:
        for (; buflen > 0; --buflen, ++buf) {
            if (isprint (*buf)) {
                fputc (*buf, stdout);
            } else {
                fputc ('.', stdout);
            }
        }
        fputc ('\n', stdout);
        break;
    case NC_ECHO_QUOTED:
        fputc ('"', stdout);
        for (; buflen > 0; --buflen, ++buf) {
            switch (*buf) {
            case '\n':
                fprintf (stdout, "\\n");
                break;
            case '\r':
                fprintf (stdout, "\\r");
                break;
            case '\\':
            case '\"':
                fprintf (stdout, "\\%c", *buf);
                break;
            default:
                if (isprint (*buf)) {
                    fputc (*buf, stdout);
                } else {
                    fprintf (stdout, "\\x%02x", *buf);
                }
            }
        }
        fprintf (stdout, "\"\n");
        break;
    case NC_ECHO_MSGPACK:
        if (buflen < 256) {
            fputc ('\xc4', stdout);
            fputc (buflen, stdout);
            fwrite (buf, 1, buflen, stdout);
        } else if (buflen < 65536) {
            fputc ('\xc5', stdout);
            fputc (buflen >> 8, stdout);
            fputc (buflen & 0xff, stdout);
            fwrite (buf, 1, buflen, stdout);
        } else {
            fputc ('\xc6', stdout);
            fputc (buflen >> 24, stdout);
            fputc ((buflen >> 16) & 0xff, stdout);
            fputc ((buflen >> 8) & 0xff, stdout);
            fputc (buflen & 0xff, stdout);
            fwrite (buf, 1, buflen, stdout);
        }
        break;
    }
    fflush (stdout);
}

void nc_connect_socket (nc_options_t *options, int sock)
{
    int i;
    int rc;

    for (i = 0; i < options->bind_addresses.num; ++i) {
        rc = nn_bind (sock, options->bind_addresses.items[i]);
        nc_assert_errno (rc >= 0, "Can't bind");
    }
    for (i = 0; i < options->connect_addresses.num; ++i) {
        rc = nn_connect (sock, options->connect_addresses.items[i]);
        nc_assert_errno (rc >= 0, "Can't connect");
    }
}

void nc_send_loop (nc_options_t *options, int sock)
{
    int rc;
    double start_time, time_to_sleep;

    for (;;) {
        start_time = nc_time ();
        rc = nn_send (sock,
            options->data_to_send.data, options->data_to_send.length,
            0);
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf (stderr, "Message not sent (EAGAIN)\n");
        } else {
            nc_assert_errno (rc >= 0, "Can't send");
        }
        if (options->send_interval >= 0) {
            time_to_sleep = (start_time + options->send_interval) - nc_time ();
            if (time_to_sleep > 0) {
                nc_sleep (time_to_sleep);
            }
        } else {
            break;
        }
    }
}

void nc_recv_loop (nc_options_t *options, int sock)
{
    int rc;
    void *buf;

    for (;;) {
        rc = nn_recv (sock, &buf, NN_MSG, 0);
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        } else if (errno == ETIMEDOUT || errno == EFSM) {
            return;  /*  No more messages possible  */
        } else {
            nc_assert_errno (rc >= 0, "Can't recv");
        }
        nc_print_message (options, buf, rc);
        nn_freemsg (buf);
    }
}

void nc_rw_loop (nc_options_t *options, int sock)
{
    int rc;
    void *buf;
    double start_time, time_to_sleep;

    for (;;) {
        start_time = nc_time ();
        rc = nn_send (sock,
            options->data_to_send.data, options->data_to_send.length,
            0);
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf (stderr, "Message not sent (EAGAIN)\n");
        } else {
            nc_assert_errno (rc >= 0, "Can't send");
        }
        if (options->send_interval < 0) {  /*  Never send any more  */
            nc_recv_loop (options, sock);
            return;
        }

        for (;;) {
            time_to_sleep = (start_time + options->send_interval) - nc_time ();
            if (time_to_sleep <= 0) {
                break;
            }
            if (options->recv_timeout >= 0 &&
                time_to_sleep > options->recv_timeout)
            {
                time_to_sleep = options->recv_timeout;
            }
            nc_set_recv_timeout (sock, time_to_sleep);
            rc = nn_recv (sock, &buf, NN_MSG, 0);
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                } else if (errno == ETIMEDOUT || errno == EFSM) {
                    time_to_sleep = (start_time + options->send_interval) \
                        - nc_time ();
                    if (time_to_sleep > 0)
                        nc_sleep (time_to_sleep);
                    continue;
                }
            }
            nc_assert_errno (rc >= 0, "Can't recv");
            nc_print_message (options, buf, rc);
            nn_freemsg (buf);
        }
    }
}

void nc_resp_loop (nc_options_t *options, int sock)
{
    int rc;
    void *buf;

    for (;;) {
        rc = nn_recv (sock, &buf, NN_MSG, 0);
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
        } else {
            nc_assert_errno (rc >= 0, "Can't recv");
        }
        nc_print_message (options, buf, rc);
        nn_freemsg (buf);
        rc = nn_send (sock,
            options->data_to_send.data, options->data_to_send.length,
            0);
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf (stderr, "Message not sent (EAGAIN)\n");
        } else {
            nc_assert_errno (rc >= 0, "Can't send");
        }
    }
}

int main (int argc, char **argv)
{
    int sock;
    nc_options_t options = {
        .verbose = 0,
        .socket_type = 0,
        .bind_addresses = {NULL, 0},
        .connect_addresses = {NULL, 0},
        .send_timeout = -1.f,
        .send_interval = -1.f,
        .recv_timeout = -1.f,
        .subscriptions = {NULL, 0},
        .data_to_send = {NULL, 0},
        .echo_format = NC_NO_ECHO
        };

    nc_parse_options (&nc_cli, &options, argc, argv);
    sock = nc_create_socket (&options);
    nc_connect_socket (&options, sock);

    switch (options.socket_type) {
    case NN_PUB:
    case NN_PUSH:
        nc_send_loop (&options, sock);
        break;
    case NN_SUB:
    case NN_PULL:
        nc_recv_loop (&options, sock);
        break;
    case NN_BUS:
    case NN_PAIR:
        if (options.data_to_send.data) {
            nc_rw_loop (&options, sock);
        } else {
            nc_recv_loop (&options, sock);
        }
        break;
    case NN_SURVEYOR:
    case NN_REQ:
        nc_rw_loop (&options, sock);
        break;
    case NN_REP:
    case NN_RESPONDENT:
        if (options.data_to_send.data) {
            nc_resp_loop (&options, sock);
        } else {
            nc_recv_loop (&options, sock);
        }
        break;
    }

    nn_close (sock);
}
