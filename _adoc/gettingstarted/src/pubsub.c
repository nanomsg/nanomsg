#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nanomsg/nn.h>
#include <nanomsg/pubsub.h>

#define SERVER "server"
#define CLIENT "client"

void
fatal(const char *func)
{
        fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
}

char *
date(void)
{
        time_t now = time(&now);
        struct tm *info = localtime(&now);
        char *text = asctime(info);
        text[strlen(text)-1] = '\0'; // remove '\n'
        return (text);
}

int
server(const char *url)
{
        int sock;

        if ((sock = nn_socket(AF_SP, NN_PUB)) < 0) {
                fatal("nn_socket");
        }
          if (nn_bind(sock, url) < 0) {
                fatal("nn_bind");
        }
        for (;;) {
                char *d = date();
                int sz_d = strlen(d) + 1; // '\0' too
                printf("SERVER: PUBLISHING DATE %s\n", d);
                int bytes = nn_send(sock, d, sz_d, 0);
                if (bytes < 0) {
                        fatal("nn_send");
                }
                sleep(1);
        }
}

int
client(const char *url, const char *name)
{
        int sock;

        if ((sock = nn_socket(AF_SP, NN_SUB)) < 0) {
                fatal("nn_socket");
        }

        // subscribe to everything ("" means all topics)
        if (nn_setsockopt(sock, NN_SUB, NN_SUB_SUBSCRIBE, "", 0) < 0) {
                fatal("nn_setsockopt");
        }
        if (nn_connect(sock, url) < 0) {
                fatal("nn_connet");
        }
        for (;;) {
                char *buf = NULL;
                int bytes = nn_recv(sock, &buf, NN_MSG, 0);
                if (bytes < 0) {
                        fatal("nn_recv");
                }
                printf("CLIENT (%s): RECEIVED %s\n", name, buf); // <1>
                nn_freemsg(buf);
        }
}

int
main(const int argc, const char **argv)
{
        if ((argc >= 2) && (strcmp(SERVER, argv[1]) == 0))
                return (server(argv[2]));

          if ((argc >= 3) && (strcmp(CLIENT, argv[1]) == 0))
                return (client (argv[2], argv[3]));

        fprintf(stderr, "Usage: pubsub %s|%s <URL> <ARG> ...\n",
            SERVER, CLIENT);
        return 1;
}
