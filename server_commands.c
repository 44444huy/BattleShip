#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "server.h"

static int send_ready_players(
    const struct Client clients[MAX_CLIENTS], int client_index)
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

int process_message(struct Client clients[MAX_CLIENTS],
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
