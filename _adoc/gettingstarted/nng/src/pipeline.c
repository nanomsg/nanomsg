#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nng/nng.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>

#define NODE0 "node0"
#define NODE1 "node1"

void
fatal(const char *func, int rv)
{
        fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
        exit(1);
}

int
node0(const char *url)
{
        nng_socket sock;
        int rv;

        if ((rv = nng_pull0_open(&sock)) != 0) {
                fatal("nng_pull0_open", rv);
        }
        if ((rv = nng_listen(sock, url, NULL, 0)) != 0) {
                fatal("nng_listen", rv);
        }
        for (;;) {
                char *buf = NULL;
                size_t sz;
                if ((rv = nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC)) != 0) {
                        fatal("nng_recv", rv);
                }
                printf("NODE0: RECEIVED \"%s\"\n", buf); // <1>
                nng_free(buf, sz);
        }
}

int
node1(const char *url, char *msg)
{
        int sz_msg = strlen(msg) + 1; // '\0' too
        nng_socket sock;
        int rv;
        int bytes;

        if ((rv = nng_push0_open(&sock)) != 0) {
                fatal("nng_push0_open", rv);
        }
        if ((rv = nng_dial(sock, url, NULL, 0)) != 0) {
                fatal("nng_dial", rv);
        }
        printf("NODE1: SENDING \"%s\"\n", msg);
        if ((rv = nng_send(sock, msg, strlen(msg)+1, 0)) != 0) {
                fatal("nng_send", rv);
        }
        sleep(1); // wait for messages to flush before shutting down
        nng_close(sock);
	return (0);
}

int
main(int argc, char **argv)
{
        if ((argc > 1) && (strcmp(NODE0, argv[1]) == 0))
                return (node0(argv[2]));

        if ((argc > 2) && (strcmp(NODE1, argv[1]) == 0))
                return (node1(argv[2], argv[3]));

        fprintf(stderr, "Usage: pipeline %s|%s <URL> <ARG> ...'\n",
                NODE0, NODE1);
        return (1);
}
