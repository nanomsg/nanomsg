#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nanomsg/nn.h>
#include <nanomsg/bus.h>

void
fatal(const char *func)
{
        fprintf(stderr, "%s: %s\n", func, nn_strerror(nn_errno()));
        exit(1);
}

int
node(const int argc, const char **argv)
{
        int sock;

        if ((sock = nn_socket (AF_SP, NN_BUS)) < 0) {
                fatal("nn_socket");
        }
          if (nn_bind(sock, argv[2]) < 0) {
                fatal("nn_bind");
        }

        sleep(1); // wait for peers to bind
        if (argc >= 3) {
                for (int x = 3; x < argc; x++) {
                        if (nn_connect(sock, argv[x]) < 0) {
                                fatal("nn_connect");
                        }
                }
        }
        sleep(1); // wait for connections
        int to = 100;
        if (nn_setsockopt(sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to,
            sizeof (to)) < 0) {
                fatal("nn_setsockopt");
        }

        // SEND
        int sz_n = strlen(argv[1]) + 1; // '\0' too
        printf("%s: SENDING '%s' ONTO BUS\n", argv[1], argv[1]);
        if (nn_send(sock, argv[1], sz_n, 0) < 0) {
                fatal("nn_send");
        }

        // RECV
        for (;;) {
                char *buf = NULL;
                int recv = nn_recv(sock, &buf, NN_MSG, 0);
                if (recv >= 0) {
                        printf("%s: RECEIVED '%s' FROM BUS\n", argv[1], buf); // <1>
                        nn_freemsg(buf);
                }
        }
        return (nn_shutdown(sock, 0));
}

int
main(int argc, const char **argv)
{
        if (argc >= 3) {
                return (node(argc, argv));
        }
        fprintf(stderr, "Usage: bus <NODE_NAME> <URL> <URL> ...\n");
        return 1;
}
