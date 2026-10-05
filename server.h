#ifndef SERVER_H
#define SERVER_H

#include <stddef.h>
#include "account.h"
#include "game.h"

#define MESSAGE_SIZE 256
#define RECEIVE_CHUNK_SIZE 512
#define MAX_CLIENTS 100
#define MAX_GAMES (MAX_CLIENTS / 2)
#define ACCOUNT_FILE_NAME "accounts.txt"

struct GameSession
{
    int active;
    int client_indices[PLAYER_COUNT];
    int draw_offered_by;
    int paused;
    int paused_by;
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
    int last_opponent_index;
    int rematch_from;
    char username[USERNAME_SIZE];
};

void initialize_clients(struct Client clients[MAX_CLIENTS]);
void initialize_game_sessions(struct GameSession games[MAX_GAMES]);
int find_free_game(const struct GameSession games[MAX_GAMES]);
int find_free_client(const struct Client clients[MAX_CLIENTS]);
void cancel_all_challenges(struct Client clients[MAX_CLIENTS],
                           int client_index);
void cancel_rematch_requests(struct Client clients[MAX_CLIENTS],
                             int client_index);
void leave_match(struct Client clients[MAX_CLIENTS],
                 struct GameSession games[MAX_GAMES],
                 int client_index);
void remove_client(struct Client clients[MAX_CLIENTS],
                   struct GameSession games[MAX_GAMES],
                   int client_index);
int find_online_client(const struct Client clients[MAX_CLIENTS],
                       const char *username);
int is_username_online(const struct Client clients[MAX_CLIENTS],
                       const char *username);

int create_game_session(struct GameSession games[MAX_GAMES],
                        struct Client clients[MAX_CLIENTS],
                        int first_client, int second_client);
int send_next_ship(const struct Client *client,
                   const struct GameSession *session);
int send_own_board(const struct Client *client,
                   const struct GameSession *session);
int send_opponent_board(const struct Client *client,
                        const struct GameSession *session);
int handle_place_command(struct Client clients[MAX_CLIENTS],
                         struct GameSession games[MAX_GAMES],
                         int client_index, const char *message,
                         char response[MESSAGE_SIZE]);
int handle_shoot_command(struct Client clients[MAX_CLIENTS],
                         struct GameSession games[MAX_GAMES],
                         int client_index, const char *message,
                         char response[MESSAGE_SIZE]);
int handle_resign_command(struct Client clients[MAX_CLIENTS],
                          struct GameSession games[MAX_GAMES],
                          int client_index,
                          char response[MESSAGE_SIZE]);
int handle_draw_command(struct Client clients[MAX_CLIENTS],
                        struct GameSession games[MAX_GAMES],
                        int client_index,
                        char response[MESSAGE_SIZE]);
int handle_accept_draw_command(struct Client clients[MAX_CLIENTS],
                               struct GameSession games[MAX_GAMES],
                               int client_index,
                               char response[MESSAGE_SIZE]);
int handle_decline_draw_command(struct Client clients[MAX_CLIENTS],
                                struct GameSession games[MAX_GAMES],
                                int client_index,
                                char response[MESSAGE_SIZE]);
int handle_pause_command(struct Client clients[MAX_CLIENTS],
                         struct GameSession games[MAX_GAMES],
                         int client_index,
                         char response[MESSAGE_SIZE]);
int handle_resume_command(struct Client clients[MAX_CLIENTS],
                          struct GameSession games[MAX_GAMES],
                          int client_index,
                          char response[MESSAGE_SIZE]);

int process_message(struct Client clients[MAX_CLIENTS],
                    struct GameSession games[MAX_GAMES],
                    int client_index,
                    struct AccountStore *accounts,
                    const char *message);
int receive_client_data(struct Client clients[MAX_CLIENTS],
                        struct GameSession games[MAX_GAMES],
                        int client_index,
                        struct AccountStore *accounts);
void accept_new_client(int listen_fd,
                       struct Client clients[MAX_CLIENTS],
                       struct GameSession games[MAX_GAMES]);

#endif
