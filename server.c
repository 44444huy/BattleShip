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
#include "game.h"
#include "protocol.h"

#define LISTEN_BACKLOG 5
#define MESSAGE_SIZE 256
#define RECEIVE_CHUNK_SIZE 512
#define MAX_CLIENTS 100
#define MAX_GAMES (MAX_CLIENTS / 2)
#define ACCOUNT_FILE_NAME "accounts.txt"

struct GameSession
{
    int active;
    int client_indices[PLAYER_COUNT];
    struct Game game;
};

struct Client
{
    int fd;
    char buffer[MESSAGE_SIZE];
    size_t used;
    int discarding_line;
    int logged_in;
    int ready;
    int challenges_from[MAX_CLIENTS];
    int opponent_index;
    int game_index;
    int game_player_index;
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

static void initialize_game_sessions(
    struct GameSession games[MAX_GAMES])
{
    for (int i = 0; i < MAX_GAMES; i++)
    {
        games[i].active = 0;
        games[i].client_indices[0] = -1;
        games[i].client_indices[1] = -1;
    }
}

static int find_free_game(const struct GameSession games[MAX_GAMES])
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

static void cancel_all_challenges(
    struct Client clients[MAX_CLIENTS], int client_index)
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

static void leave_match(struct Client clients[MAX_CLIENTS],
                        struct GameSession games[MAX_GAMES],
                        int client_index)
{
    struct Client *client = &clients[client_index];

    if (client->game_index >= 0 &&
        client->game_index < MAX_GAMES)
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

    if (opponent->fd != -1 &&
        opponent->opponent_index == client_index)
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

static void remove_client(struct Client clients[MAX_CLIENTS],
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

static int find_online_client(const struct Client clients[MAX_CLIENTS],
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

static int is_username_online(const struct Client clients[MAX_CLIENTS],
                              const char *username)
{
    return find_online_client(clients, username) != -1;
}

static int send_ready_players(const struct Client clients[MAX_CLIENTS],
                              int client_index)
{
    int ready_count = 0;
    char response[MESSAGE_SIZE];

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i != client_index &&
            clients[i].fd != -1 &&
            clients[i].logged_in &&
            clients[i].ready &&
            clients[i].opponent_index == -1)
        {
            ready_count++;
        }
    }

    snprintf(response, sizeof(response),
             "READY_LIST %d", ready_count);

    if (send_line(clients[client_index].fd, response) == -1)
    {
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i != client_index &&
            clients[i].fd != -1 &&
            clients[i].logged_in &&
            clients[i].ready &&
            clients[i].opponent_index == -1)
        {
            snprintf(response, sizeof(response),
                     "READY_PLAYER %s", clients[i].username);

            if (send_line(clients[client_index].fd, response) == -1)
            {
                return -1;
            }
        }
    }

    return send_line(clients[client_index].fd,
                     "READY_LIST_END");
}

static int send_challenge_list(
    const struct Client clients[MAX_CLIENTS], int client_index)
{
    int challenge_count = 0;
    char response[MESSAGE_SIZE];

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[client_index].challenges_from[i] &&
            clients[i].fd != -1 && clients[i].logged_in)
        {
            challenge_count++;
        }
    }

    snprintf(response, sizeof(response),
             "CHALLENGE_LIST %d", challenge_count);

    if (send_line(clients[client_index].fd, response) == -1)
    {
        return -1;
    }

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[client_index].challenges_from[i] &&
            clients[i].fd != -1 && clients[i].logged_in)
        {
            snprintf(response, sizeof(response),
                     "CHALLENGER %s", clients[i].username);

            if (send_line(clients[client_index].fd, response) == -1)
            {
                return -1;
            }
        }
    }

    return send_line(clients[client_index].fd,
                     "CHALLENGE_LIST_END");
}

static int create_game_session(
    struct GameSession games[MAX_GAMES],
    struct Client clients[MAX_CLIENTS],
    int first_client, int second_client)
{
    int game_index = find_free_game(games);

    if (game_index == -1)
    {
        return -1;
    }

    games[game_index].active = 1;
    games[game_index].client_indices[0] = first_client;
    games[game_index].client_indices[1] = second_client;
    initialize_game(&games[game_index].game);

    clients[first_client].game_index = game_index;
    clients[first_client].game_player_index = 0;
    clients[second_client].game_index = game_index;
    clients[second_client].game_player_index = 1;

    return game_index;
}

static int send_next_ship(
    const struct Client *client,
    const struct GameSession *session)
{
    const struct Player *player =
        &session->game.players[client->game_player_index];
    char message[MESSAGE_SIZE];

    for (int i = 0; i < FLEET_SIZE; i++)
    {
        if (!player->fleet[i].placed)
        {
            snprintf(message, sizeof(message),
                     "PLACE_NEXT %d %s %d",
                     i + 1, player->fleet[i].name,
                     player->fleet[i].length);
            return send_line(client->fd, message);
        }
    }

    return send_line(client->fd, "FLEET_READY");
}

static int send_own_board(const struct Client *client,
                          const struct GameSession *session)
{
    const struct Player *player =
        &session->game.players[client->game_player_index];
    char message[MESSAGE_SIZE];

    if (send_line(client->fd, "BOARD_BEGIN OWN") == -1)
    {
        return -1;
    }

    for (int row = 0; row < BOARD_SIZE; row++)
    {
        snprintf(message, sizeof(message),
                 "BOARD_ROW %c %.*s", 'A' + row,
                 BOARD_SIZE, player->board[row]);

        if (send_line(client->fd, message) == -1)
        {
            return -1;
        }
    }

    return send_line(client->fd, "BOARD_END");
}

static int send_opponent_board(
    const struct Client *client,
    const struct GameSession *session)
{
    int opponent_player = 1 - client->game_player_index;
    char view[BOARD_SIZE][BOARD_SIZE];
    char message[MESSAGE_SIZE];

    create_opponent_view(
        session->game.players[opponent_player].board, view);

    if (send_line(client->fd, "BOARD_BEGIN OPPONENT") == -1)
    {
        return -1;
    }

    for (int row = 0; row < BOARD_SIZE; row++)
    {
        snprintf(message, sizeof(message),
                 "BOARD_ROW %c %.*s", 'A' + row,
                 BOARD_SIZE, view[row]);

        if (send_line(client->fd, message) == -1)
        {
            return -1;
        }
    }

    return send_line(client->fd, "BOARD_END");
}

static struct Ship *find_ship_at(struct Player *player,
                                 int row, int column)
{
    for (int i = 0; i < FLEET_SIZE; i++)
    {
        struct Ship *ship = &player->fleet[i];

        for (int part = 0; part < ship->length; part++)
        {
            int ship_row = ship->row;
            int ship_column = ship->column;

            if (ship->direction == HORIZONTAL)
            {
                ship_column += part;
            }
            else
            {
                ship_row += part;
            }

            if (ship_row == row && ship_column == column)
            {
                return ship;
            }
        }
    }

    return NULL;
}

static void finish_game_session(
    struct Client clients[MAX_CLIENTS],
    struct GameSession games[MAX_GAMES], int game_index)
{
    struct GameSession *session = &games[game_index];

    for (int i = 0; i < PLAYER_COUNT; i++)
    {
        int client_index = session->client_indices[i];

        if (client_index >= 0 && client_index < MAX_CLIENTS)
        {
            clients[client_index].opponent_index = -1;
            clients[client_index].game_index = -1;
            clients[client_index].game_player_index = -1;
            clients[client_index].ready = 0;
        }
    }

    session->active = 0;
    session->client_indices[0] = -1;
    session->client_indices[1] = -1;
}

static int handle_place_command(
    struct Client clients[MAX_CLIENTS],
    struct GameSession games[MAX_GAMES],
    int client_index, const char *message,
    char response[MESSAGE_SIZE])
{
    struct Client *client = &clients[client_index];
    char command[16];
    int ship_number;
    char coordinate[16];
    char direction_text[8];
    char extra_character;
    int fields = sscanf(message, "%15s %d %15s %7s %c",
                        command, &ship_number, coordinate,
                        direction_text, &extra_character);

    if (!client->logged_in)
    {
        strcpy(response, "ERROR Login required");
        return 0;
    }

    if (client->game_index < 0 ||
        client->game_index >= MAX_GAMES ||
        !games[client->game_index].active)
    {
        strcpy(response, "ERROR Not in a match");
        return 0;
    }

    if (fields != 4)
    {
        strcpy(response,
               "ERROR Use: PLACE <ship 1-5> <A1-J10> <H|V>");
        return 0;
    }

    if (ship_number < 1 || ship_number > FLEET_SIZE)
    {
        strcpy(response, "ERROR Invalid ship number");
        return 0;
    }

    int row;
    int column;
    int direction;

    if (!parse_coordinate(coordinate, &row, &column) ||
        !parse_direction(direction_text, &direction))
    {
        strcpy(response, "ERROR Invalid coordinate or direction");
        return 0;
    }

    struct GameSession *session = &games[client->game_index];
    struct Player *player =
        &session->game.players[client->game_player_index];
    struct Ship *ship = &player->fleet[ship_number - 1];

    if (ship->placed)
    {
        strcpy(response, "ERROR Ship already placed");
        return 0;
    }

    if (!place_ship(player->board, ship, row, column, direction))
    {
        strcpy(response, "ERROR Ship does not fit or overlaps");
        return 0;
    }

    snprintf(response, MESSAGE_SIZE,
             "PLACE_OK %d %s %s %s",
             ship_number, ship->name,
             coordinate, direction_text);

    if (send_line(client->fd, response) == -1)
    {
        return -1;
    }

    if (!all_ships_placed(player->fleet))
    {
        return send_next_ship(client, session) == -1 ? -1 : 1;
    }

    if (send_line(client->fd, "FLEET_READY") == -1)
    {
        return -1;
    }

    if (game_is_ready(&session->game))
    {
        int first_index = session->client_indices[0];
        int second_index = session->client_indices[1];

        if (send_line(clients[first_index].fd, "GAME_READY") == -1 ||
            send_line(clients[second_index].fd, "GAME_READY") == -1 ||
            send_line(clients[first_index].fd, "YOUR_TURN") == -1 ||
            send_line(clients[second_index].fd,
                      "OPPONENT_TURN") == -1)
        {
            return -1;
        }
    }

    return 1;
}

static int handle_shoot_command(
    struct Client clients[MAX_CLIENTS],
    struct GameSession games[MAX_GAMES],
    int client_index, const char *message,
    char response[MESSAGE_SIZE])
{
    struct Client *client = &clients[client_index];
    char command[16];
    char coordinate[16];
    char extra_character;
    int fields = sscanf(message, "%15s %15s %c",
                        command, coordinate, &extra_character);

    if (!client->logged_in)
    {
        strcpy(response, "ERROR Login required");
        return 0;
    }

    if (client->game_index < 0 ||
        client->game_index >= MAX_GAMES ||
        !games[client->game_index].active)
    {
        strcpy(response, "ERROR Not in a match");
        return 0;
    }

    if (fields != 2)
    {
        strcpy(response, "ERROR Use: SHOOT <A1-J10>");
        return 0;
    }

    int row;
    int column;

    if (!parse_coordinate(coordinate, &row, &column))
    {
        strcpy(response, "ERROR Invalid coordinate");
        return 0;
    }

    struct GameSession *session = &games[client->game_index];

    if (!game_is_ready(&session->game))
    {
        strcpy(response, "ERROR Both fleets are not ready");
        return 0;
    }

    if (session->game.current_turn != client->game_player_index)
    {
        strcpy(response, "ERROR Not your turn");
        return 0;
    }

    int defender_player = 1 - client->game_player_index;
    int defender_index = session->client_indices[defender_player];
    struct Player *defender = &session->game.players[defender_player];
    int result = play_turn(&session->game, row, column);

    if (result == SHOT_ALREADY_TAKEN)
    {
        strcpy(response, "ERROR Coordinate already shot");
        return 0;
    }

    if (result != SHOT_HIT && result != SHOT_MISS)
    {
        strcpy(response, "ERROR Shot could not be processed");
        return 0;
    }

    const char *result_text = result == SHOT_HIT ? "HIT" : "MISS";
    snprintf(response, MESSAGE_SIZE,
             "SHOT_RESULT %s %s", coordinate, result_text);

    if (send_line(client->fd, response) == -1)
    {
        return -1;
    }

    snprintf(response, MESSAGE_SIZE,
             "OPPONENT_SHOT %s %s", coordinate, result_text);
    send_line(clients[defender_index].fd, response);

    if (result == SHOT_HIT)
    {
        struct Ship *hit_ship = find_ship_at(defender, row, column);

        if (hit_ship != NULL &&
            is_ship_sunk(defender->board, hit_ship))
        {
            snprintf(response, MESSAGE_SIZE,
                     "SHIP_SUNK %s", hit_ship->name);
            send_line(client->fd, response);

            snprintf(response, MESSAGE_SIZE,
                     "YOUR_SHIP_SUNK %s", hit_ship->name);
            send_line(clients[defender_index].fd, response);
        }
    }

    if (session->game.game_over)
    {
        send_line(client->fd, "GAME_RESULT WIN");
        send_line(clients[defender_index].fd,
                  "GAME_RESULT LOSE");
        finish_game_session(clients, games,
                            client->game_index);
        return 1;
    }

    int current_player = session->game.current_turn;
    int waiting_player = 1 - current_player;
    int current_client = session->client_indices[current_player];
    int waiting_client = session->client_indices[waiting_player];

    send_line(clients[current_client].fd, "YOUR_TURN");
    send_line(clients[waiting_client].fd, "OPPONENT_TURN");
    return 1;
}

static int process_message(struct Client clients[MAX_CLIENTS],
                           struct GameSession games[MAX_GAMES],
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
            client->ready = 0;
            client->opponent_index = -1;
            client->game_index = -1;
            client->game_player_index = -1;
            strcpy(client->username, username);

            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                client->challenges_from[i] = 0;
            }

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
            cancel_all_challenges(clients, client_index);
            leave_match(clients, games, client_index);
            client->logged_in = 0;
            client->ready = 0;
            client->username[0] = '\0';
            strcpy(response, "LOGOUT_OK");
        }
    }
    else if (strcmp(message, "READY") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (client->opponent_index != -1)
        {
            strcpy(response, "ERROR Already in a match");
        }
        else
        {
            client->ready = 1;
            strcpy(response, "READY_OK");
        }
    }
    else if (strcmp(message, "UNREADY") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (client->opponent_index != -1)
        {
            strcpy(response, "ERROR Already in a match");
        }
        else
        {
            cancel_all_challenges(clients, client_index);
            client->ready = 0;
            strcpy(response, "UNREADY_OK");
        }
    }
    else if (strcmp(message, "LIST_READY") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else
        {
            if (send_ready_players(clients, client_index) == -1)
            {
                perror("send");
                return 1;
            }

            return 0;
        }
    }
    else if (strcmp(message, "LIST_CHALLENGES") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else
        {
            if (send_challenge_list(clients, client_index) == -1)
            {
                perror("send");
                return 1;
            }

            return 0;
        }
    }
    else if (strcmp(command, "CHALLENGE") == 0)
    {
        char target_username[USERNAME_SIZE];
        char extra_character;
        int fields = sscanf(message, "%15s %31s %c",
                            command, target_username,
                            &extra_character);

        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (fields != 2)
        {
            strcpy(response,
                   "ERROR Use: CHALLENGE <username>");
        }
        else if (!client->ready)
        {
            strcpy(response, "ERROR You must be READY");
        }
        else if (client->opponent_index != -1)
        {
            strcpy(response, "ERROR Already in a match");
        }
        else
        {
            int target_index = find_online_client(clients,
                                                  target_username);

            if (target_index == -1)
            {
                strcpy(response, "ERROR Player is not online");
            }
            else if (target_index == client_index)
            {
                strcpy(response, "ERROR Cannot challenge yourself");
            }
            else
            {
                struct Client *target = &clients[target_index];

                if (!target->ready || target->opponent_index != -1)
                {
                    strcpy(response, "ERROR Player is not available");
                }
                else if (target->challenges_from[client_index])
                {
                    strcpy(response, "ERROR Challenge already sent");
                }
                else if (client->challenges_from[target_index])
                {
                    strcpy(response,
                           "ERROR This player already challenged you");
                }
                else
                {
                    char notification[MESSAGE_SIZE];

                    target->challenges_from[client_index] = 1;
                    snprintf(notification, sizeof(notification),
                             "CHALLENGE_FROM %s", client->username);

                    if (send_line(target->fd, notification) == -1)
                    {
                        target->challenges_from[client_index] = 0;
                        strcpy(response,
                               "ERROR Could not contact player");
                    }
                    else
                    {
                        snprintf(response, sizeof(response),
                                 "CHALLENGE_SENT %s",
                                 target->username);
                    }
                }
            }
        }
    }
    else if (strcmp(command, "ACCEPT") == 0)
    {
        char challenger_username[USERNAME_SIZE];
        char extra_character;
        int fields = sscanf(message, "%15s %31s %c",
                            command, challenger_username,
                            &extra_character);

        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (fields != 2)
        {
            strcpy(response, "ERROR Use: ACCEPT <username>");
        }
        else if (client->opponent_index != -1)
        {
            strcpy(response, "ERROR Already in a match");
        }
        else
        {
            int challenger_index = find_online_client(
                clients, challenger_username);

            if (challenger_index == -1 ||
                !client->challenges_from[challenger_index])
            {
                strcpy(response, "ERROR No matching challenge");
            }
            else
            {
                struct Client *challenger = &clients[challenger_index];
                char notification[MESSAGE_SIZE];

                if (find_free_game(games) == -1)
                {
                    strcpy(response, "ERROR Server game capacity reached");
                }
                else
                {
                    client->challenges_from[challenger_index] = 0;
                    snprintf(notification, sizeof(notification),
                             "CHALLENGE_ACCEPTED %s", client->username);

                    if (send_line(challenger->fd, notification) == -1)
                    {
                        strcpy(response,
                               "ERROR Could not contact challenger");
                    }
                    else
                    {
                        cancel_all_challenges(clients, client_index);
                        cancel_all_challenges(clients, challenger_index);
                        client->opponent_index = challenger_index;
                        challenger->opponent_index = client_index;
                        client->ready = 0;
                        challenger->ready = 0;

                        int game_index = create_game_session(
                            games, clients,
                            challenger_index, client_index);

                        snprintf(notification, sizeof(notification),
                                 "MATCH_START %s 1", client->username);

                        if (game_index == -1 ||
                            send_line(challenger->fd,
                                      notification) == -1 ||
                            send_next_ship(challenger,
                                           &games[game_index]) == -1)
                        {
                            leave_match(clients, games,
                                        challenger_index);
                            strcpy(response,
                                   "ERROR Could not start match");
                        }
                        else
                        {
                            snprintf(notification,
                                     sizeof(notification),
                                     "ACCEPT_OK %s",
                                     challenger->username);

                            if (send_line(client->fd,
                                          notification) == -1)
                            {
                                return 1;
                            }

                            snprintf(notification,
                                     sizeof(notification),
                                     "MATCH_START %s 2",
                                     challenger->username);

                            if (send_line(client->fd,
                                          notification) == -1 ||
                                send_next_ship(client,
                                               &games[game_index]) == -1)
                            {
                                return 1;
                            }

                            return 0;
                        }
                    }
                }
            }
        }
    }
    else if (strcmp(command, "DECLINE") == 0)
    {
        char challenger_username[USERNAME_SIZE];
        char extra_character;
        int fields = sscanf(message, "%15s %31s %c",
                            command, challenger_username,
                            &extra_character);

        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (fields != 2)
        {
            strcpy(response, "ERROR Use: DECLINE <username>");
        }
        else
        {
            int challenger_index = find_online_client(
                clients, challenger_username);

            if (challenger_index == -1 ||
                !client->challenges_from[challenger_index])
            {
                strcpy(response, "ERROR No matching challenge");
            }
            else
            {
                struct Client *challenger = &clients[challenger_index];
                char notification[MESSAGE_SIZE];

                client->challenges_from[challenger_index] = 0;
                snprintf(notification, sizeof(notification),
                         "CHALLENGE_DECLINED %s", client->username);
                send_line(challenger->fd, notification);
                snprintf(response, sizeof(response),
                         "DECLINE_OK %s", challenger->username);
            }
        }
    }
    else if (strcmp(command, "PLACE") == 0)
    {
        int place_result = handle_place_command(
            clients, games, client_index, message, response);

        if (place_result < 0)
        {
            return 1;
        }

        if (place_result > 0)
        {
            return 0;
        }
    }
    else if (strcmp(command, "SHOOT") == 0)
    {
        int shoot_result = handle_shoot_command(
            clients, games, client_index, message, response);

        if (shoot_result < 0)
        {
            return 1;
        }

        if (shoot_result > 0)
        {
            return 0;
        }
    }
    else if (strcmp(message, "SHOW_BOARD") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (client->game_index < 0 ||
                 client->game_index >= MAX_GAMES ||
                 !games[client->game_index].active)
        {
            strcpy(response, "ERROR Not in a match");
        }
        else
        {
            if (send_own_board(client,
                               &games[client->game_index]) == -1)
            {
                return 1;
            }

            return 0;
        }
    }
    else if (strcmp(message, "SHOW_OPPONENT") == 0)
    {
        if (!client->logged_in)
        {
            strcpy(response, "ERROR Login required");
        }
        else if (client->game_index < 0 ||
                 client->game_index >= MAX_GAMES ||
                 !games[client->game_index].active)
        {
            strcpy(response, "ERROR Not in a match");
        }
        else
        {
            if (send_opponent_board(
                    client, &games[client->game_index]) == -1)
            {
                return 1;
            }

            return 0;
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

static int receive_client_data(struct Client clients[MAX_CLIENTS],
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

static void accept_new_client(int listen_fd,
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
