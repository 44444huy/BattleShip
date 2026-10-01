#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "account.h"
#include "protocol.h"

#define LISTEN_BACKLOG 5
#define MESSAGE_SIZE 256
#define RECEIVE_CHUNK_SIZE 512
#define MAX_CLIENTS 100
#define ACCOUNT_FILE_NAME "accounts.txt"

struct Client
{
    int fd;
    char buffer[MESSAGE_SIZE];
    size_t used;
    int discarding_line;
    int logged_in;
    char username[USERNAME_SIZE];
};

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

static void initialize_clients(struct Client clients[MAX_CLIENTS])
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].used = 0;
        clients[i].discarding_line = 0;
        clients[i].logged_in = 0;
        clients[i].username[0] = '\0';
    }
}

static int find_free_client(const struct Client clients[MAX_CLIENTS])
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == -1)
        {
            return i;
        }
    }

    return -1;
}

static void remove_client(struct Client *client)
{
    close(client->fd);
    client->fd = -1;
    client->used = 0;
    client->discarding_line = 0;
    client->logged_in = 0;
    client->username[0] = '\0';
}

static int is_username_online(const struct Client clients[MAX_CLIENTS],
                              const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 &&
            clients[i].logged_in &&
            strcmp(clients[i].username, username) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int process_message(struct Client clients[MAX_CLIENTS],
                           int client_index,
                           struct AccountStore *accounts,
                           const char *message)
{
    struct Client *client = &clients[client_index];
    char response[MESSAGE_SIZE];
    char command[16] = "";
    int close_connection = 0;

    sscanf(message, "%15s", command);
    printf("Client %d sent command: %s\n",
           client_index + 1, command);

    if (strcmp(message, "HELLO") == 0)
    {
        strcpy(response, "WELCOME");
    }
    else if (strcmp(message, "PING") == 0)
    {
        strcpy(response, "PONG");
    }
    else if (strcmp(command, "REGISTER") == 0)
    {
        char username[USERNAME_SIZE];
        char password[PASSWORD_SIZE];
        char extra_character;
        int fields = sscanf(message, "%15s %31s %63s %c",
                            command, username, password,
                            &extra_character);

        if (client->logged_in)
        {
            strcpy(response, "ERROR Logout before registering");
        }
        else if (fields != 3)
        {
            strcpy(response,
                   "ERROR Use: REGISTER <username> <password>");
        }
        else if (!valid_username(username) ||
                 !valid_password(password))
        {
            strcpy(response, "ERROR Invalid username or password format");
        }
        else
        {
            int result = register_account(accounts,
                                          ACCOUNT_FILE_NAME,
                                          username, password);

            if (result == REGISTER_ACCOUNT_OK)
            {
                strcpy(response, "REGISTER_OK");
            }
            else if (result == REGISTER_ACCOUNT_EXISTS)
            {
                strcpy(response, "ERROR Username already exists");
            }
            else if (result == REGISTER_ACCOUNT_FULL)
            {
                strcpy(response, "ERROR Account storage is full");
            }
            else
            {
                strcpy(response, "ERROR Could not save account");
            }
        }
    }
    else if (strcmp(command, "LOGIN") == 0)
    {
        char username[USERNAME_SIZE];
        char password[PASSWORD_SIZE];
        char extra_character;
        int fields = sscanf(message, "%15s %31s %63s %c",
                            command, username, password,
                            &extra_character);

        if (client->logged_in)
        {
            strcpy(response, "ERROR Already logged in");
        }
        else if (fields != 3)
        {
            strcpy(response,
                   "ERROR Use: LOGIN <username> <password>");
        }
        else if (!authenticate_account(accounts, username, password))
        {
            strcpy(response, "ERROR Invalid username or password");
        }
        else if (is_username_online(clients, username))
        {
            strcpy(response, "ERROR Account already online");
        }
        else
        {
            client->logged_in = 1;
            strcpy(client->username, username);
            snprintf(response, sizeof(response),
                     "LOGIN_OK %s", username);
        }
    }
    else if (strcmp(message, "LOGOUT") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Not logged in");
        }
        else
        {
            client->logged_in = 0;
            client->username[0] = '\0';
            strcpy(response, "LOGOUT_OK");
        }
    }
    else if (strcmp(message, "QUIT") == 0)
    {
        strcpy(response, "BYE");
        close_connection = 1;
    }
    else
    {
        strcpy(response, "ERROR Unknown command");
    }

    if (send_line(client->fd, response) == -1)
    {
        perror("send");
        return 1;
    }

    return close_connection;
}

static int process_received_bytes(struct Client clients[MAX_CLIENTS],
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

            if (process_message(clients, client_index, accounts,
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

static int receive_client_data(struct Client clients[MAX_CLIENTS],
                               int client_index,
                               struct AccountStore *accounts)
{
    struct Client *client = &clients[client_index];
    char data[RECEIVE_CHUNK_SIZE];
    ssize_t received = recv(client->fd, data, sizeof(data), 0);

    if (received > 0)
    {
        return process_received_bytes(clients, client_index, accounts,
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

static void accept_new_client(int listen_fd,
                              struct Client clients[MAX_CLIENTS])
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
    clients[client_index].username[0] = '\0';

    char client_ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &client_address.sin_addr,
              client_ip, sizeof(client_ip));

    printf("Client %d connected from %s:%d\n",
           client_index + 1, client_ip,
           ntohs(client_address.sin_port));

    if (send_line(client_fd, "CONNECTED") == -1)
    {
        perror("send");
        remove_client(&clients[client_index]);
    }
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
    initialize_clients(clients);

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
            accept_new_client(listen_fd, clients);
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].fd != -1 &&
                FD_ISSET(clients[i].fd, &read_fds))
            {
                if (receive_client_data(clients, i, &accounts))
                {
                    remove_client(&clients[i]);
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1)
        {
            remove_client(&clients[i]);
        }
    }

    close(listen_fd);
    return 0;
}
