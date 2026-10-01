#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "server.h"

int create_game_session(struct GameSession games[MAX_GAMES],
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

int send_next_ship(const struct Client *client,
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

int send_own_board(const struct Client *client,
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

int send_opponent_board(const struct Client *client,
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

int handle_place_command(struct Client clients[MAX_CLIENTS],
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

int handle_shoot_command(struct Client clients[MAX_CLIENTS],
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
