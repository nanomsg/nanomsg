#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nng/nng.h>
#include <nng/protocol/survey0/survey.h>
#include <nng/protocol/survey0/respond.h>

#define SERVER "server"
#define CLIENT "client"
#define DATE   "DATE"

void
fatal(const char *func, int rv)
{
        fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
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
        nng_socket sock;
	int rv;

        if ((rv = nng_surveyor0_open(&sock)) != 0) {
                fatal("nng_surveyor0_open", rv);
        }
        if ((rv = nng_listen(sock, url, NULL, 0)) != 0) {
                fatal("nng_listen", rv);
        }
        for (;;) {
                printf("SERVER: SENDING DATE SURVEY REQUEST\n");
                if ((rv = nng_send(sock, DATE, strlen(DATE) + 1, 0)) != 0) {
                        fatal("nng_send", rv);
                }

                for (;;) {
                        char *buf = NULL;
			size_t sz;
                        rv = nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC);
			if (rv == NNG_ETIMEDOUT) {
				break;
			}
			if (rv != 0) {
				fatal("nng_recv", rv);
			}
                        printf("SERVER: RECEIVED \"%s\" SURVEY RESPONSE\n",
                            buf); // <1>
                        nng_free(buf, sz);
                }

                printf("SERVER: SURVEY COMPLETE\n");
        }
}

int
client(const char *url, const char *name)
{
        nng_socket sock;
	int rv;

        if ((rv = nng_respondent0_open(&sock)) != 0) {
                fatal("nng_respondent0_open", rv);
        }
        if ((rv = nng_dial(sock, url, NULL, NNG_FLAG_NONBLOCK)) != 0) {
                fatal("nng_dial", rv);
        }
	for (;;) {
                char *buf = NULL;
		size_t sz;
                if ((rv = nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC)) == 0) {
                        printf("CLIENT (%s): RECEIVED \"%s\" SURVEY REQUEST\n",
                            name, buf); // <1>
                        nng_free(buf, sz);
                        char *d = date();
                        printf("CLIENT (%s): SENDING DATE SURVEY RESPONSE\n",
                           name);
                        if ((rv = nng_send(sock, d, strlen(d) + 1, 0)) != 0) {
                                fatal("nng_send", rv);
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
