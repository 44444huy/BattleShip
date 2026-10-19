#ifndef SERVER_H
#define SERVER_H
#include <stddef.h>
#include <stdint.h>
#include "account.h"
#include "game.h"
#include "storage.h"

#define MESSAGE_SIZE 256
#define RECEIVE_CHUNK_SIZE 512
#define OUTPUT_CAPACITY 65536
#define MAX_CLIENTS 100
#define MAX_GAMES (MAX_CLIENTS / 2)
#define TURN_MS 30000
#define PLACEMENT_MS 120000

enum MatchSource { CHALLENGE_MATCH, REMATCH_MATCH, MATCHMAKING_MATCH };
struct Client {
    int fd, closing, failed;
    char buffer[MESSAGE_SIZE];
    size_t used;
    int discarding_line;
    char output[OUTPUT_CAPACITY];
    size_t output_used;
    int logged_in, ready;
    int64_t queued_at; /* Monotonic milliseconds; repeated READY preserves this. */
    uint64_t queue_order;
    struct Account account;
    int challenges_from[MAX_CLIENTS];
    int game_index, game_player_index;
    int last_opponent_index, rematch_from;
};
struct GameSession {
    int active, playing;
    sqlite3_int64 id;
    enum MatchSource source;
    int client_indices[PLAYER_COUNT];
    int draw_offered_by, paused, paused_by;
    /* Monotonic deadlines; remaining_ms is saved while play is suspended. */
    int64_t started_at, placement_deadline, turn_deadline, remaining_ms;
    struct Game game;
};
/* Injectable sources let tests advance time and reproduce generated fleets. */
typedef int64_t (*ServerClock)(void *context);
typedef int (*ServerRandom)(void *context, uint32_t *value);
struct Server {
    sqlite3 *db;
    struct Client clients[MAX_CLIENTS];
    struct GameSession games[MAX_GAMES];
    ServerClock clock;
    void *clock_context;
    ServerRandom random;
    void *random_context;
    uint64_t queue_order;
    int shutting_down;
};

void initialize_server(struct Server *server, sqlite3 *db);
void reset_client(struct Client *client);
int64_t server_now(struct Server *server);
int server_random(void *context, uint32_t *value);
void emit(struct Server *server, int client, const char *format, ...);
void emit_both(struct Server *server, struct GameSession *game, const char *format, ...);
int find_online_client(struct Server *server, const char *username);
void cancel_challenges(struct Server *server, int client);
void cancel_rematches(struct Server *server, int client);
void logout_client(struct Server *server, int client, const char *reason);
void remove_client(struct Server *server, int client);
int start_match(struct Server *server, int first, int second, enum MatchSource source);
int finish_match(struct Server *server, int game, int winner, const char *reason);
void abort_match(struct Server *server, int game);
int record_event(struct Server *server, int game, const char *format, ...);
int matchmaking_gap(int64_t waiting_ms);
void run_matchmaking(struct Server *server);
void process_deadlines(struct Server *server);
int64_t next_wakeup_ms(struct Server *server);
int generate_random_fleet(struct Player *player, ServerRandom random, void *context);
void handle_game_command(struct Server *server, int client, const char *command, int argc, char **argv);
void send_board(struct Server *server, int client, int opponent);
void process_message(struct Server *server, int client, const char *message);
void send_query(struct Server *server, int client, const char *command, int argc, char **argv);
void accept_new_client(struct Server *server, int listen_fd);
int receive_client_data(struct Server *server, int client);
int flush_client(struct Server *server, int client);
#endif
