#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server.h"

#define LISTEN_BACKLOG 5

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

    struct AccountStore accounts;

    if (!load_accounts(&accounts, ACCOUNT_FILE_NAME))
    {
        fprintf(stderr, "Cannot load %s.\n", ACCOUNT_FILE_NAME);
        return 1;
    }

    printf("Loaded %d account(s).\n", accounts.count);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (listen_fd == -1)
    {
        perror("socket");
        return 1;
    }

    int reuse_address = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1)
    {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

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

    struct Client clients[MAX_CLIENTS];
    struct GameSession games[MAX_GAMES];
    initialize_clients(clients);
    initialize_game_sessions(games);

    while (1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        int max_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1)
            {
                FD_SET(clients[i].fd, &read_fds);

                if (clients[i].fd > max_fd)
                {
                    max_fd = clients[i].fd;
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

        if (FD_ISSET(listen_fd, &read_fds))
        {
            accept_new_client(listen_fd, clients, games);
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1 &&
                FD_ISSET(clients[i].fd, &read_fds))
            {
                if (receive_client_data(clients, games,
                                        i, &accounts))
                {
                    remove_client(clients, games, i);
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1)
        {
            remove_client(clients, games, i);
        }
    }

    close(listen_fd);
    return 0;
}
