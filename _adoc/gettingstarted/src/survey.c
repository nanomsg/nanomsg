#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nanomsg/nn.h>
#include <nanomsg/survey.h>

#define SERVER "server"
#define CLIENT "client"
#define DATE   "DATE"

void
fatal(const char *func)
{
        fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
        exit(1);
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

        if ((sock = nn_socket (AF_SP, NN_SURVEYOR)) < 0) {
                fatal("nn_socket");
        }
        if (nn_bind(sock, url)  < 0) {
                fatal("nn_bind");
        }
        for (;;) {
                printf("SERVER: SENDING DATE SURVEY REQUEST\n");
                int bytes = nn_send(sock, DATE, strlen(DATE) + 1, 0);
                if (bytes < 0) {
                        fatal("nn_send");
                }

                for (;;) {
                        char *buf = NULL;
                        int bytes = nn_recv(sock, &buf, NN_MSG, 0);
                        if (bytes < 0) {
                                if (nn_errno() == ETIMEDOUT) {
                                        break;
                                }
                                fatal("nn_recv");
                        }
                        printf("SERVER: RECEIVED \"%s\" SURVEY RESPONSE\n",
                            buf); // <1>
                        nn_freemsg(buf);
                }

                printf("SERVER: SURVEY COMPLETE\n");
                sleep(1); // Start another survey in a second
        }
}

int
client(const char *url, const char *name)
{
        int sock;

        if ((sock = nn_socket(AF_SP, NN_RESPONDENT)) < 0) {
                fatal("nn_socket");
        }
        if (nn_connect (sock, url) < 0) {
                fatal("nn_connect");
        }
            for (;;) {
                char *buf = NULL;
                int bytes = nn_recv(sock, &buf, NN_MSG, 0);
                if (bytes >= 0) {
                        printf("CLIENT (%s): RECEIVED \"%s\" SURVEY REQUEST\n",
                            name, buf); // <1>
                        nn_freemsg(buf);
                        char *d = date();
                        int sz_d = strlen(d) + 1; // '\0' too
                        printf("CLIENT (%s): SENDING DATE SURVEY RESPONSE\n",
                           name);
                        if (nn_send(sock, d, sz_d, 0) < 0) {
                                fatal("nn_send");
                        }
                }
        }
}

int
main(const int argc, const char **argv)
{
        if ((argc >= 2) && (strcmp(SERVER, argv[1]) == 0))
                return (server(argv[2]));

        if ((argc >= 3) && (strcmp(CLIENT, argv[1]) == 0))
                return (client(argv[2], argv[3]));

        fprintf(stderr, "Usage: survey %s|%s <URL> <ARG> ...\n",
            SERVER, CLIENT);
        return 1;
}
