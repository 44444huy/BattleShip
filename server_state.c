#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <openssl/rand.h>
#include "server.h"

static int64_t monotonic_ms(void *context)
{
    (void)context;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) abort();
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
int server_random(void *context, uint32_t *value)
{
    (void)context;
    return RAND_bytes((unsigned char *)value, sizeof(*value)) == 1;
}
int64_t server_now(struct Server *server) { return server->clock(server->clock_context); }
void reset_client(struct Client *client)
{
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->game_index = client->game_player_index = -1;
    client->last_opponent_index = client->rematch_from = -1;
}
void initialize_server(struct Server *server, sqlite3 *db)
{
    memset(server, 0, sizeof(*server));
    server->db = db;
    server->clock = monotonic_ms;
    server->random = server_random;
    for (int i = 0; i < MAX_CLIENTS; ++i) reset_client(&server->clients[i]);
}
void emit(struct Server *server, int index, const char *format, ...)
{
    if (index < 0 || index >= MAX_CLIENTS) return;
    struct Client *client = &server->clients[index];
    if (client->fd == -1 || client->failed) return;
    char line[MESSAGE_SIZE];
    va_list args; va_start(args, format);
    int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || length >= MESSAGE_SIZE ||
        client->output_used + (size_t)length + 1 > OUTPUT_CAPACITY) {
        client->failed = 1;
        return;
    }
    memcpy(client->output + client->output_used, line, (size_t)length);
    client->output_used += (size_t)length;
    client->output[client->output_used++] = '\n';
}
void emit_both(struct Server *server, struct GameSession *game, const char *format, ...)
{
    char line[MESSAGE_SIZE];
    va_list args; va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (n < 0 || n >= MESSAGE_SIZE) return;
    for (int p = 0; p < 2; ++p) emit(server, game->client_indices[p], "%s", line);
}
int find_online_client(struct Server *server, const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client *c = &server->clients[i];
        /* Reserve the account until disconnect finalization has completed. */
        if (c->fd != -1 && c->logged_in &&
            strcmp(c->account.username, username) == 0) return i;
    }
    return -1;
}
void cancel_challenges(struct Server *server, int index)
{
    struct Client *client = &server->clients[index];
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client->challenges_from[i] || server->clients[i].challenges_from[index]) {
            client->challenges_from[i] = server->clients[i].challenges_from[index] = 0;
            emit(server, i, "CHALLENGE_CANCELLED %s", client->account.username);
        }
    }
}
void cancel_rematches(struct Server *server, int index)
{
    struct Client *c = &server->clients[index];
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client *other = &server->clients[i];
        if (other->rematch_from == index || c->rematch_from == i) {
            other->rematch_from = -1;
            emit(server, i, "REMATCH_CANCELLED %s", c->account.username);
        }
        if (other->last_opponent_index == index) other->last_opponent_index = -1;
    }
    c->last_opponent_index = c->rematch_from = -1;
}
void logout_client(struct Server *server, int index, const char *reason)
{
    struct Client *c = &server->clients[index];
    c->ready = 0;
    if (c->game_index >= 0) {
        if (server->shutting_down) abort_match(server, c->game_index);
        else finish_match(server, c->game_index, 1 - c->game_player_index, reason);
    }
    cancel_challenges(server, index);
    cancel_rematches(server, index);
    c->logged_in = 0;
    memset(&c->account, 0, sizeof(c->account));
}
void remove_client(struct Server *server, int index)
{
    struct Client *client = &server->clients[index];
    if (client->fd == -1) return;
    logout_client(server, index, "DISCONNECT");
    close(client->fd);
    reset_client(client);
}
int matchmaking_gap(int64_t waiting_ms)
{
    if (waiting_ms >= 20000) return 200;
    return waiting_ms >= 10000 ? 150 : 100;
}
static int eligible(const struct Client *c)
{
    return c->fd != -1 && !c->failed && !c->closing && c->logged_in && c->ready && c->game_index == -1;
}
void run_matchmaking(struct Server *server)
{
    int visited[MAX_CLIENTS] = {0};
    int64_t now = server_now(server);
    for (int n = 0; n < MAX_CLIENTS; ++n) {
        int first = -1;
        for (int i = 0; i < MAX_CLIENTS; ++i)
            if (!visited[i] && eligible(&server->clients[i]) &&
                (first == -1 || server->clients[i].queue_order < server->clients[first].queue_order)) first = i;
        if (first == -1) break;
        visited[first] = 1;
        struct Client *a = &server->clients[first];
        int second = -1, best = 201, gap = matchmaking_gap(now - a->queued_at);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            if (i == first || !eligible(&server->clients[i])) continue;
            struct Client *b = &server->clients[i];
            int64_t difference = (int64_t)a->account.rating - b->account.rating;
            if (difference < 0) difference = -difference;
            if (difference <= gap && (difference < best ||
                (difference == best && (second == -1 || b->queue_order < server->clients[second].queue_order)))) {
                second = i; best = (int)difference;
            }
        }
        if (second != -1 && !start_match(server, first, second, MATCHMAKING_MATCH)) {
            /* A storage/capacity error should not spin, nor silently dequeue users. */
            break;
        }
    }
}
