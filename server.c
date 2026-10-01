#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

#define LISTEN_BACKLOG 5
#define MESSAGE_SIZE 256
#define CLIENT_COUNT 2

static int parse_port(const char *text, int *port)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || text[0] == '\0' || end[0] != '\0' ||
        value < 1 || value > 65535)
    {
        return 0;
    }

    *port = (int)value;
    return 1;
}

int main(int argc, char *argv[])
{
    int port;

    if (argc != 2 || !parse_port(argv[1], &port))
    {
        fprintf(stderr, "Usage: %s <port 1-65535>\n", argv[0]);
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1)
    {
        perror("socket");
        return 1;
    }

    int reuse_address = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               &reuse_address, sizeof(reuse_address));

    struct sockaddr_in server_address = {0};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&server_address,
             sizeof(server_address)) == -1)
    {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, LISTEN_BACKLOG) == -1)
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    printf("Server is listening on port %d...\n", port);

    int client_fds[CLIENT_COUNT] = {-1, -1};

    for (int i = 0; i < CLIENT_COUNT; i++)
    {
        struct sockaddr_in client_address = {0};
        socklen_t client_length = sizeof(client_address);
        int client_fd = accept(listen_fd,
                               (struct sockaddr *)&client_address,
                               &client_length);

        if (client_fd == -1)
        {
            perror("accept");

            for (int j = 0; j < i; j++)
            {
                close(client_fds[j]);
            }

            close(listen_fd);
            return 1;
        }

        client_fds[i] = client_fd;

        char client_ip[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &client_address.sin_addr,
                  client_ip, sizeof(client_ip));
        printf("Player %d connected from %s:%d\n",
               i + 1, client_ip, ntohs(client_address.sin_port));

        char assignment[MESSAGE_SIZE];
        snprintf(assignment, sizeof(assignment), "PLAYER %d", i + 1);

        if (send_line(client_fd, assignment) == -1)
        {
            perror("send");
        }
    }

    close(listen_fd);

    for (int i = 0; i < CLIENT_COUNT; i++)
    {
        if (send_line(client_fds[i], "MATCH_READY") == -1)
        {
            perror("send");
        }
    }

    int active_clients = CLIENT_COUNT;

    while (active_clients > 0)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        int max_fd = -1;

        for (int i = 0; i < CLIENT_COUNT; i++)
        {
            if (client_fds[i] != -1)
            {
                FD_SET(client_fds[i], &read_fds);

                if (client_fds[i] > max_fd)
                {
                    max_fd = client_fds[i];
                }
            }
        }

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        for (int i = 0; i < CLIENT_COUNT; i++)
        {
            int client_fd = client_fds[i];

            if (client_fd == -1 || !FD_ISSET(client_fd, &read_fds))
            {
                continue;
            }

            char message[MESSAGE_SIZE];
            int receive_result = receive_line(client_fd,
                                              message, sizeof(message));

            if (receive_result == 1)
            {
                const char *response;
                int close_connection = 0;

                printf("Player %d sent: %s\n", i + 1, message);

                if (strcmp(message, "HELLO") == 0)
                {
                    response = "WELCOME";
                }
                else if (strcmp(message, "PING") == 0)
                {
                    response = "PONG";
                }
                else if (strcmp(message, "QUIT") == 0)
                {
                    response = "BYE";
                    close_connection = 1;
                }
                else
                {
                    response = "ERROR Unknown command";
                }

                if (send_line(client_fd, response) == -1)
                {
                    perror("send");
                    close_connection = 1;
                }

                if (close_connection)
                {
                    close(client_fd);
                    client_fds[i] = -1;
                    active_clients--;
                }
            }
            else if (receive_result == RECEIVE_LINE_TOO_LONG)
            {
                if (send_line(client_fd, "ERROR Message too long") == -1)
                {
                    perror("send");
                    close(client_fd);
                    client_fds[i] = -1;
                    active_clients--;
                }
            }
            else
            {
                if (receive_result == 0)
                {
                    printf("Player %d disconnected.\n", i + 1);
                }
                else
                {
                    perror("recv");
                }

                close(client_fd);
                client_fds[i] = -1;
                active_clients--;
            }
        }
    }

    for (int i = 0; i < CLIENT_COUNT; i++)
    {
        if (client_fds[i] != -1)
        {
            close(client_fds[i]);
        }
    }

    return 0;
}
