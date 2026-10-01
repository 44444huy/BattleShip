#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "protocol.h"
#include "server.h"

void initialize_clients(struct Client clients[MAX_CLIENTS])
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].used = 0;
        clients[i].discarding_line = 0;
        clients[i].logged_in = 0;
        clients[i].ready = 0;
        clients[i].opponent_index = -1;
        clients[i].game_index = -1;
        clients[i].game_player_index = -1;
        clients[i].username[0] = '\0';

        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            clients[i].challenges_from[j] = 0;
        }
    }
}

void initialize_game_sessions(struct GameSession games[MAX_GAMES])
{
    for (int i = 0; i < MAX_GAMES; i++)
    {
        games[i].active = 0;
        games[i].client_indices[0] = -1;
        games[i].client_indices[1] = -1;
    }
}

int find_free_game(const struct GameSession games[MAX_GAMES])
{
    for (int i = 0; i < MAX_GAMES; i++)
    {
        if (!games[i].active)
        {
            return i;
        }
    }

    return -1;
}

int find_free_client(const struct Client clients[MAX_CLIENTS])
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

void cancel_all_challenges(struct Client clients[MAX_CLIENTS],
                           int client_index)
{
    struct Client *client = &clients[client_index];
    char notification[MESSAGE_SIZE];

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client->challenges_from[i])
        {
            client->challenges_from[i] = 0;

            if (clients[i].fd != -1)
            {
                snprintf(notification, sizeof(notification),
                         "CHALLENGE_CANCELLED %s", client->username);
                send_line(clients[i].fd, notification);
            }
        }

        if (clients[i].challenges_from[client_index])
        {
            clients[i].challenges_from[client_index] = 0;

            if (clients[i].fd != -1)
            {
                snprintf(notification, sizeof(notification),
                         "CHALLENGE_CANCELLED %s", client->username);
                send_line(clients[i].fd, notification);
            }
        }
    }
}

void leave_match(struct Client clients[MAX_CLIENTS],
                 struct GameSession games[MAX_GAMES],
                 int client_index)
{
    struct Client *client = &clients[client_index];

    if (client->game_index >= 0 && client->game_index < MAX_GAMES)
    {
        int game_index = client->game_index;

        games[game_index].active = 0;
        games[game_index].client_indices[0] = -1;
        games[game_index].client_indices[1] = -1;
    }

    if (client->opponent_index == -1)
    {
        client->game_index = -1;
        client->game_player_index = -1;
        return;
    }

    int opponent_index = client->opponent_index;
    struct Client *opponent = &clients[opponent_index];
    client->opponent_index = -1;
    client->game_index = -1;
    client->game_player_index = -1;

    if (opponent->fd != -1 && opponent->opponent_index == client_index)
    {
        char notification[MESSAGE_SIZE];

        opponent->opponent_index = -1;
        opponent->game_index = -1;
        opponent->game_player_index = -1;
        snprintf(notification, sizeof(notification),
                 "OPPONENT_LEFT %s", client->username);
        send_line(opponent->fd, notification);
    }
}

void remove_client(struct Client clients[MAX_CLIENTS],
                   struct GameSession games[MAX_GAMES],
                   int client_index)
{
    struct Client *client = &clients[client_index];

    cancel_all_challenges(clients, client_index);
    leave_match(clients, games, client_index);
    close(client->fd);
    client->fd = -1;
    client->used = 0;
    client->discarding_line = 0;
    client->logged_in = 0;
    client->ready = 0;
    client->opponent_index = -1;
    client->game_index = -1;
    client->game_player_index = -1;
    client->username[0] = '\0';

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client->challenges_from[i] = 0;
    }
}

int find_online_client(const struct Client clients[MAX_CLIENTS],
                       const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1 &&
            clients[i].logged_in &&
            strcmp(clients[i].username, username) == 0)
        {
            return i;
        }
    }

    return -1;
}

int is_username_online(const struct Client clients[MAX_CLIENTS],
                       const char *username)
{
    return find_online_client(clients, username) != -1;
}
