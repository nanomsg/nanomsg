/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#include "dns.h"
#include "literal.h"

#include "../../utils/err.h"
#include "../../utils/cont.h"

#include <string.h>

#ifndef NN_HAVE_WINDOWS
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#define NN_DNS_STATE_IDLE 1
#define NN_DNS_STATE_DONE 2

/*  Private functions. */
static void nn_dns_handler (struct nn_fsm *self, void *source, int type);

void nn_dns_init (struct nn_dns *self, struct nn_fsm *owner)
{
    nn_fsm_init (&self->fsm, nn_dns_handler, owner);
    self->state = NN_DNS_STATE_IDLE;
    self->error = -1;
    nn_fsm_event_init (&self->done);
}

void nn_dns_term (struct nn_dns *self)
{
    nn_assert (self->state == NN_DNS_STATE_IDLE);

    nn_fsm_event_term (&self->done);
    nn_fsm_term (&self->fsm);
}

int nn_dns_isidle (struct nn_dns *self)
{
    return nn_fsm_isidle (&self->fsm);
}

void nn_dns_start (struct nn_dns *self, const char *addr, size_t addrlen,
    int ipv4only)
{
    int rc;
    struct addrinfo query;
    struct addrinfo *reply;
    char hostname [NN_SOCKADDR_MAX];

    /*  Try to resolve the supplied string as a literal address. In this case,
        there's no DNS lookup involved. */
    rc = nn_literal_resolve (addr, addrlen, ipv4only, &self->ss, &self->sslen);
    if (rc == 0) {
        self->error = 0;
        nn_fsm_start (&self->fsm);
        return;
    }
    errnum_assert (rc == -EINVAL, -rc);

    /*  The name is not a literal. Let's do an actual DNS lookup. */
    memset (&query, 0, sizeof (query));
    if (ipv4only)
        query.ai_family = AF_INET;
    else {
        query.ai_family = AF_INET6;
#ifdef AI_V4MAPPED
        query.ai_flags = AI_V4MAPPED;
#endif
    }
    nn_assert (sizeof (hostname) > addrlen);
    query.ai_socktype = SOCK_STREAM;
    memcpy (hostname, addr, addrlen);
    hostname [addrlen] = 0;

    /*  Perform the DNS lookup itself. */
    self->error = getaddrinfo (hostname, NULL, &query, &reply);
    if (self->error) {
        nn_fsm_start (&self->fsm);
        return;
    }

    /*  Check that exactly one address is returned and store it. */
    nn_assert (reply && !reply->ai_next);
    memcpy (&self->ss, reply->ai_addr, reply->ai_addrlen);
    self->sslen = reply->ai_addrlen;

    freeaddrinfo (reply);
    
    nn_fsm_start (&self->fsm);
}

void nn_dns_stop (struct nn_dns *self)
{
    nn_fsm_stop (&self->fsm);
}

int nn_dns_getaddr (struct nn_dns *self, struct sockaddr_storage **result,
    size_t *resultlen)
{
    if (nn_slow (self->error != 0))
        return -EFAULT;

    *result = &self->ss;
    *resultlen = self->sslen;
    return 0;
}

static void nn_dns_handler (struct nn_fsm *self, void *source, int type)
{
    struct nn_dns *dns;

    dns = nn_cont (self, struct nn_dns, fsm);

/******************************************************************************/
/*  STOP procedure.                                                           */
/******************************************************************************/
    if (nn_slow (source == &dns->fsm && type == NN_FSM_STOP)) {
        nn_fsm_stopped (&dns->fsm, dns, NN_DNS_STOPPED);
        dns->state = NN_DNS_STATE_IDLE;
        return;
    }

    switch (dns->state) {
/******************************************************************************/
/*  IDLE state.                                                               */
/******************************************************************************/
    case NN_DNS_STATE_IDLE:
        if (source == &dns->fsm) {
            switch (type) {
            case NN_FSM_START:
                nn_fsm_raise (&dns->fsm, &dns->done, dns,
                    dns->error ? NN_DNS_ERROR : NN_DNS_DONE);
                self->state = NN_DNS_STATE_DONE;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  DONE state.                                                               */
/******************************************************************************/
    case NN_DNS_STATE_DONE:
        nn_assert (0);

/******************************************************************************/
/*  Invalid state.                                                            */
/******************************************************************************/
    default:
        nn_assert (0);
    }
}

