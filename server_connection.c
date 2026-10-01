#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"
#include "server.h"

static int process_received_bytes(
    struct Client clients[MAX_CLIENTS],
    struct GameSession games[MAX_GAMES],
    int client_index,
    struct AccountStore *accounts,
    const char *data,
    size_t length)
{
    struct Client *client = &clients[client_index];

    for (size_t i = 0; i < length; i++)
    {
        char character = data[i];

        if (client->discarding_line)
        {
            if (character == '\n')
            {
                client->discarding_line = 0;
            }

            continue;
        }

        if (character == '\r')
        {
            continue;
        }

        if (character == '\n')
        {
            client->buffer[client->used] = '\0';

            if (process_message(clients, games, client_index, accounts,
                                client->buffer))
            {
                return 1;
            }

            client->used = 0;
            continue;
        }

        if (client->used + 1 >= sizeof(client->buffer))
        {
            if (send_line(client->fd, "ERROR Message too long") == -1)
            {
                perror("send");
                return 1;
            }

            client->used = 0;
            client->discarding_line = 1;
            continue;
        }

        client->buffer[client->used] = character;
        client->used++;
    }

    return 0;
}

int receive_client_data(struct Client clients[MAX_CLIENTS],
                        struct GameSession games[MAX_GAMES],
                        int client_index,
                        struct AccountStore *accounts)
{
    struct Client *client = &clients[client_index];
    char data[RECEIVE_CHUNK_SIZE];
    ssize_t received = recv(client->fd, data, sizeof(data), 0);

    if (received > 0)
    {
        return process_received_bytes(clients, games,
                                      client_index, accounts,
                                      data, (size_t)received);
    }

    if (received == 0)
    {
        printf("Client %d disconnected.\n", client_index + 1);
        return 1;
    }

    if (errno == EINTR)
    {
        return 0;
    }

    perror("recv");
    return 1;
}

void accept_new_client(int listen_fd,
                       struct Client clients[MAX_CLIENTS],
                       struct GameSession games[MAX_GAMES])
{
    struct sockaddr_in client_address = {0};
    socklen_t client_length = sizeof(client_address);
    int client_fd = accept(listen_fd,
                           (struct sockaddr *)&client_address,
                           &client_length);

    if (client_fd == -1)
    {
        perror("accept");
        return;
    }

    int client_index = find_free_client(clients);

    if (client_index == -1)
    {
        send_line(client_fd, "ERROR Server full");
        close(client_fd);
        return;
    }

    clients[client_index].fd = client_fd;
    clients[client_index].used = 0;
    clients[client_index].discarding_line = 0;
    clients[client_index].logged_in = 0;
    clients[client_index].ready = 0;
    clients[client_index].opponent_index = -1;
    clients[client_index].game_index = -1;
    clients[client_index].game_player_index = -1;
    clients[client_index].username[0] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[client_index].challenges_from[i] = 0;
    }

    char client_ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &client_address.sin_addr,
              client_ip, sizeof(client_ip));

    printf("Client %d connected from %s:%d\n",
           client_index + 1, client_ip,
           ntohs(client_address.sin_port));

    if (send_line(client_fd, "CONNECTED") == -1)
    {
        perror("send");
        remove_client(clients, games, client_index);
    }
}
