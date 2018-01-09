#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nanomsg/nn.h>
#include <nanomsg/pair.h>

#define NODE0 "node0"
#define NODE1 "node1"

void
fatal(const char *func)
{
        fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
        exit(1);
}

int
send_name(int sock, const char *name)
{
        printf("%s: SENDING \"%s\"\n", name, name);
        int sz_n = strlen(name) + 1; // '\0' too
        return (nn_send(sock, name, sz_n, 0));
}

int
recv_name(int sock, const char *name)
{
        char *buf = NULL;
        int result = nn_recv(sock, &buf, NN_MSG, 0);
        if (result > 0) {
                printf("%s: RECEIVED \"%s\"\n", name, buf); // <1>
                nn_freemsg(buf);
        }
        return (result);
}

int
send_recv(int sock, const char *name)
{
        int to = 100;
        if (nn_setsockopt(sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to,
            sizeof (to)) < 0) {
                fatal("nn_setsockopt");
        }

        for (;;) {
                recv_name(sock, name);
                sleep(1);
                send_name(sock, name);
        }
}

int
node0(const char *url)
{
        int sock;
        if ((sock = nn_socket(AF_SP, NN_PAIR)) < 0) {
                fatal("nn_socket");
        }
         if (nn_bind(sock, url) < 0) {
                fatal("nn_bind");
        }
        return (send_recv(sock, NODE0));
}

int
node1(const char *url)
{
        int sock;
        if ((sock = nn_socket(AF_SP, NN_PAIR)) < 0) {
                fatal("nn_socket");
        }
        if (nn_connect(sock, url) < 0) {
                fatal("nn_connect");
        }
        return (send_recv(sock, NODE1));
}

int
main(const int argc, const char **argv)
{
        if ((argc > 1) && (strcmp(NODE0, argv[1]) == 0))
                return (node0(argv[2]));

        if ((argc > 1) && (strcmp(NODE1, argv[1]) == 0))
                return (node1(argv[2]));

        fprintf(stderr, "Usage: pair %s|%s <URL> <ARG> ...\n", NODE0, NODE1);
        return 1;
}
