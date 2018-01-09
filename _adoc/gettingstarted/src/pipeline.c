#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>

#define NODE0 "node0"
#define NODE1 "node1"

void
fatal(const char *func)
{
        fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
        exit(1);
}

int
node0(const char *url)
{
        int sock;
        int rv;

        if ((sock = nn_socket(AF_SP, NN_PULL)) < 0) {
                fatal("nn_socket");
        }
        if ((rv = nn_bind(sock, url)) < 0) {
                fatal("nn_bind");
        }
        for (;;) {
                char *buf = NULL;
                int bytes;
                if ((bytes = nn_recv(sock, &buf, NN_MSG, 0)) < 0) {
                        fatal("nn_recv");
                }
                printf("NODE0: RECEIVED \"%s\"\n", buf); // <1>
                nn_freemsg(buf);
        }
}

int
node1(const char *url, const char *msg)
{
        int sz_msg = strlen(msg) + 1; // '\0' too
        int sock;
        int rv;
        int bytes;

        if ((sock = nn_socket(AF_SP, NN_PUSH)) < 0) {
                fatal("nn_socket");
        }
        if ((rv = nn_connect(sock, url)) < 0) {
                fatal("nn_connect");
        }
        printf("NODE1: SENDING \"%s\"\n", msg);
        if ((bytes = nn_send(sock, msg, sz_msg, 0)) < 0) {
                fatal("nn_send");
        }
        sleep(1); // wait for messages to flush before shutting down
        return (nn_shutdown(sock, 0));
}

int
main(const int argc, const char **argv)
{
        if ((argc > 1) && (strcmp(NODE0, argv[1]) == 0))
                return (node0(argv[2]));

        if ((argc > 2) && (strcmp(NODE1, argv[1]) == 0))
                return (node1(argv[2], argv[3]));

        fprintf(stderr, "Usage: pipeline %s|%s <URL> <ARG> ...'\n",
                NODE0, NODE1);
        return (1);
}
