#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

static void release_match(struct Server *s, int index, int rematch)
{
    struct GameSession *g = &s->games[index];
    for (int p = 0; p < 2; ++p) {
        struct Client *c = &s->clients[g->client_indices[p]];
        c->game_index = c->game_player_index = -1;
        c->ready = 0;
        c->rematch_from = -1;
        c->last_opponent_index = rematch ? g->client_indices[1 - p] : -1;
    }
    memset(g, 0, sizeof(*g));
}
void abort_match(struct Server *s, int index)
{
    struct GameSession *g = &s->games[index];
    if (!g->active) return;
    const char *reason = s->shutting_down ? "SERVER_SHUTDOWN" : "SERVER_ERROR";
    storage_abort(s->db, g->id, reason);
    emit_both(s, g, "MATCH_ABORTED %lld %s", g->id, reason);
    release_match(s, index, 0);
}
int record_event(struct Server *s, int index, const char *format, ...)
{
    struct GameSession *g = &s->games[index];
    char payload[180];
    va_list args; va_start(args, format);
    int n = vsnprintf(payload, sizeof(payload), format, args);
    va_end(args);
    int ok = n >= 0 && n < (int)sizeof(payload) && storage_begin(s->db);
    if (ok) ok = storage_event(s->db, g->id, server_now(s) - g->started_at, payload);
    if (ok) ok = storage_commit(s->db);
    if (!ok) { storage_rollback(s->db); abort_match(s, index); }
    return ok;
}
int finish_match(struct Server *s, int index, int winner, const char *reason)
{
    struct GameSession *g = &s->games[index];
    if (!g->active) return 0;
    int ratings[2], deltas[2];
    if (!storage_finish(s->db, g->id, winner, reason, server_now(s) - g->started_at, ratings, deltas)) {
        abort_match(s, index); return 0;
    }
    for (int p = 0; p < 2; ++p) {
        int client = g->client_indices[p];
        s->clients[client].account.rating = ratings[p];
        emit(s, client, "GAME_RESULT %s %s %lld %d %d", winner == -1 ? "DRAW" : winner == p ? "WIN" : "LOSE",
             reason, g->id, ratings[p], deltas[p]);
    }
    release_match(s, index, 1);
    return 1;
}
int start_match(struct Server *s, int first, int second, enum MatchSource source)
{
    struct Client *a = &s->clients[first], *b = &s->clients[second];
    if (first == second || !a->logged_in || !b->logged_in || a->fd < 0 || b->fd < 0 ||
        a->failed || b->failed || a->closing || b->closing || a->game_index != -1 || b->game_index != -1) return 0;
    int index = -1;
    for (int i = 0; i < MAX_GAMES; ++i) if (!s->games[i].active) { index = i; break; }
    if (index == -1) { emit(s, first, "ERROR Server game capacity reached"); return 0; }
    const char *name = source == MATCHMAKING_MATCH ? "MATCHMAKING" : source == REMATCH_MATCH ? "REMATCH" : "CHALLENGE";
    int ratings[2] = {a->account.rating, b->account.rating};
    sqlite3_int64 id = storage_start(s->db, a->account.id, b->account.id, name, ratings);
    if (!id) { emit(s, first, "ERROR Storage unavailable"); emit(s, second, "ERROR Storage unavailable"); return 0; }
    cancel_challenges(s, first); cancel_challenges(s, second);
    cancel_rematches(s, first); cancel_rematches(s, second);
    struct GameSession *g = &s->games[index];
    memset(g, 0, sizeof(*g));
    g->active = 1; g->id = id; g->source = source;
    g->client_indices[0] = first; g->client_indices[1] = second;
    g->draw_offered_by = g->paused_by = -1;
    g->started_at = server_now(s); g->placement_deadline = g->started_at + PLACEMENT_MS;
    initialize_game(&g->game);
    for (int p = 0; p < 2; ++p) {
        int ci = g->client_indices[p];
        struct Client *c = &s->clients[ci];
        c->ready = 0; c->game_index = index; c->game_player_index = p;
        if (source == MATCHMAKING_MATCH) emit(s, ci, "MATCH_FOUND %s", s->clients[g->client_indices[1-p]].account.username);
        emit(s, ci, "MATCH_START");
        emit(s, ci, "FLEET_BEGIN %d %d", FLEET_SIZE, PLACEMENT_MS);
        for (int j = 0; j < FLEET_SIZE; ++j)
            emit(s, ci, "FLEET_SHIP %d %s %d", j+1, g->game.players[p].fleet[j].name, g->game.players[p].fleet[j].length);
        emit(s, ci, "FLEET_END");
    }
    return 1;
}
static void announce_turn(struct Server *s, struct GameSession *g)
{
    int64_t remaining = g->turn_deadline - server_now(s);
    if (remaining < 0) remaining = 0;
    for (int p = 0; p < 2; ++p)
        emit(s, g->client_indices[p], "%s %lld", p == g->game.current_turn ? "YOUR_TURN" : "OPPONENT_TURN", (long long)remaining);
}
static void begin_play(struct Server *s, int index)
{
    struct GameSession *g = &s->games[index];
    if (!g->active || g->playing || !game_is_ready(&g->game)) return;
    if (!record_event(s, index, "GAME_READY 0")) return;
    g->playing = 1; g->remaining_ms = TURN_MS; g->turn_deadline = server_now(s) + TURN_MS;
    emit_both(s, g, "GAME_READY");
    announce_turn(s, g);
}
void send_board(struct Server *s, int ci, int opponent)
{
    struct Client *c = &s->clients[ci];
    struct GameSession *g = &s->games[c->game_index];
    char view[BOARD_SIZE][BOARD_SIZE];
    const struct Player *player = &g->game.players[opponent ? 1-c->game_player_index : c->game_player_index];
    if (opponent) create_opponent_view(player->board, view);
    else memcpy(view, player->board, sizeof(view));
    emit(s, ci, "BOARD_BEGIN %s", opponent ? "OPPONENT" : "OWN");
    for (int r = 0; r < BOARD_SIZE; ++r) emit(s, ci, "BOARD_ROW %c %.*s", 'A'+r, BOARD_SIZE, view[r]);
    emit(s, ci, "BOARD_END");
}
static int accept_fleet(struct Server *s, int index, int player, const struct Player *fleet, int automatic)
{
    struct GameSession *g = &s->games[index];
    int ok = storage_begin(s->db);
    for (int j = 0; ok && j < FLEET_SIZE; ++j) {
        const struct Ship *ship = &fleet->fleet[j];
        char event[160];
        snprintf(event, sizeof(event), "SHIP %d %d %c%d %c %d %s", player, j+1, 'A'+ship->row,
                 ship->column+1, ship->direction == HORIZONTAL ? 'H' : 'V', ship->length, automatic ? "AUTO" : "MANUAL");
        ok = storage_event(s->db, g->id, server_now(s)-g->started_at, event);
    }
    if (ok) ok = storage_commit(s->db);
    if (!ok) { storage_rollback(s->db); abort_match(s, index); return 0; }
    g->game.players[player] = *fleet;
    int ci = g->client_indices[player];
    if (automatic) {
        emit(s, ci, "AUTO_FLEET");
        /* Geometry lets a GUI draw separate adjacent ships correctly. */
        for (int j = 0; j < FLEET_SIZE; ++j) {
            const struct Ship *ship = &fleet->fleet[j];
            emit(s, ci, "OWN_SHIP %d %c%d %c %d", j+1, 'A'+ship->row, ship->column+1,
                 ship->direction == HORIZONTAL ? 'H' : 'V', ship->length);
        }
        send_board(s, ci, 0);
    }
    emit(s, ci, "FLEET_READY");
    begin_play(s, index);
    return 1;
}
static int coordinate(const char *text, int *row, int *column)
{
    size_t n = strlen(text);
    if (n < 2 || n > 3 || text[1] < '1' || text[1] > '9' ||
        (n == 3 && !(text[1] == '1' && text[2] == '0'))) return 0;
    return parse_coordinate(text, row, column);
}
static void place_fleet(struct Server *s, int ci, int argc, char **argv)
{
    struct Client *c = &s->clients[ci];
    struct GameSession *g = &s->games[c->game_index];
    if (g->playing || all_ships_placed(g->game.players[c->game_player_index].fleet)) {
        emit(s, ci, "ERROR Fleet already accepted"); return;
    }
    if (argc != 1 + FLEET_SIZE * 3) { emit(s, ci, "ERROR Use: PLACE_FLEET <id coordinate H|V> repeated five times"); return; }
    struct Player candidate; initialize_player(&candidate);
    for (int j = 1; j < argc; j += 3) {
        int row, column, direction;
        if (strlen(argv[j]) != 1 || argv[j][0] < '1' || argv[j][0] > '5' ||
            !coordinate(argv[j+1], &row, &column) || strlen(argv[j+2]) != 1 ||
            !parse_direction(argv[j+2], &direction) ||
            !place_ship(candidate.board, &candidate.fleet[argv[j][0]-'1'], row, column, direction)) {
            emit(s, ci, "ERROR Invalid fleet"); return;
        }
    }
    if (!all_ships_placed(candidate.fleet)) { emit(s, ci, "ERROR Incomplete fleet"); return; }
    accept_fleet(s, c->game_index, c->game_player_index, &candidate, 0);
}
static int sunk_ship(const struct Player *defender, int row, int column)
{
    for (int j = 0; j < FLEET_SIZE; ++j) {
        const struct Ship *ship = &defender->fleet[j];
        for (int k = 0; k < ship->length; ++k)
            if (row == ship->row + (ship->direction == VERTICAL ? k : 0) &&
                column == ship->column + (ship->direction == HORIZONTAL ? k : 0))
                return is_ship_sunk(defender->board, ship) ? j : -1;
    }
    return -1;
}
static void shoot_command(struct Server *s, int ci, int argc, char **argv)
{
    struct Client *c = &s->clients[ci];
    int index = c->game_index, actor = c->game_player_index;
    struct GameSession *g = &s->games[index];
    int row, column;
    if (argc != 2 || !coordinate(argv[1], &row, &column)) { emit(s, ci, "ERROR Use: SHOOT <A1-J10>"); return; }
    if (!g->playing) { emit(s, ci, "ERROR Both fleets are not ready"); return; }
    if (g->paused || g->draw_offered_by != -1) { emit(s, ci, "ERROR Game is suspended"); return; }
    if (g->game.current_turn != actor) { emit(s, ci, "ERROR Not your turn"); return; }
    struct Game candidate = g->game;
    int result = play_turn(&candidate, row, column);
    if (result != SHOT_HIT && result != SHOT_MISS) { emit(s, ci, "ERROR Coordinate already shot"); return; }
    int sunk = result == SHOT_HIT ? sunk_ship(&candidate.players[1-actor], row, column) : -1;
    const char *result_text = result == SHOT_HIT ? "HIT" : "MISS";
    char event[160];
    snprintf(event, sizeof(event), "SHOT %d %c%d %s", actor, 'A'+row, column+1, result_text);
    int ok = storage_begin(s->db);
    if (ok) ok = storage_event(s->db, g->id, server_now(s)-g->started_at, event);
    if (ok && sunk >= 0) {
        const struct Ship *ship = &candidate.players[1-actor].fleet[sunk];
        snprintf(event, sizeof(event), "SUNK %d %d %c%d %c %d", 1-actor, sunk+1, 'A'+ship->row, ship->column+1,
                 ship->direction == HORIZONTAL ? 'H' : 'V', ship->length);
        ok = storage_event(s->db, g->id, server_now(s)-g->started_at, event);
    }
    if (ok) ok = storage_commit(s->db);
    if (!ok) { storage_rollback(s->db); abort_match(s, index); return; }
    g->game = candidate;
    emit(s, ci, "SHOT_RESULT %c%d %s", 'A'+row, column+1, result_text);
    int defender = g->client_indices[1-actor];
    emit(s, defender, "OPPONENT_SHOT %c%d %s", 'A'+row, column+1, result_text);
    if (sunk >= 0) {
        const struct Ship *ship = &candidate.players[1-actor].fleet[sunk];
        emit(s, ci, "SHIP_SUNK %d %c%d %c %d", sunk+1, 'A'+ship->row, ship->column+1,
             ship->direction == HORIZONTAL ? 'H' : 'V', ship->length);
        emit(s, defender, "YOUR_SHIP_SUNK %d", sunk+1);
    }
    if (g->game.game_over) { finish_match(s, index, actor, "ALL_SHIPS_SUNK"); return; }
    g->turn_deadline = server_now(s) + TURN_MS;
    announce_turn(s, g);
}
static void freeze_timer(struct Server *s, struct GameSession *g)
{
    if (!g->paused && g->draw_offered_by == -1) g->remaining_ms = g->turn_deadline - server_now(s);
}
static void resume_timer(struct Server *s, struct GameSession *g)
{
    if (!g->paused && g->draw_offered_by == -1) {
        g->turn_deadline = server_now(s) + g->remaining_ms;
        announce_turn(s, g);
    }
}
void handle_game_command(struct Server *s, int ci, const char *command, int argc, char **argv)
{
    struct Client *c = &s->clients[ci];
    if (c->game_index == -1) { emit(s, ci, "ERROR Not in a match"); return; }
    int index = c->game_index, p = c->game_player_index;
    struct GameSession *g = &s->games[index];
    if (!strcmp(command, "PLACE_FLEET")) { place_fleet(s, ci, argc, argv); return; }
    if (!strcmp(command, "SHOOT")) { shoot_command(s, ci, argc, argv); return; }
    if (argc != 1) { emit(s, ci, "ERROR Unexpected arguments"); return; }
    if (!strcmp(command, "SHOW_BOARD") || !strcmp(command, "SHOW_OPPONENT")) {
        send_board(s, ci, !strcmp(command, "SHOW_OPPONENT")); return;
    }
    if (!strcmp(command, "RESIGN")) { finish_match(s, index, 1-p, "RESIGN"); return; }
    if (!g->playing) { emit(s, ci, "ERROR Both fleets are not ready"); return; }
    if (!strcmp(command, "PAUSE")) {
        if (g->paused || g->draw_offered_by != -1) { emit(s, ci, "ERROR Game is already suspended"); return; }
        /* Freeze at command handling time, not after the disk commit latency. */
        freeze_timer(s, g);
        if (!record_event(s, index, "PAUSE %d", p)) return;
        g->paused = 1; g->paused_by = p;
        emit_both(s, g, "GAME_PAUSED %s %lld", c->account.username, (long long)g->remaining_ms);
    } else if (!strcmp(command, "RESUME")) {
        if (!g->paused || g->paused_by != p) { emit(s, ci, "ERROR Only the player who paused can resume"); return; }
        if (!record_event(s, index, "RESUME %d", p)) return;
        g->paused = 0; g->paused_by = -1;
        emit_both(s, g, "GAME_RESUMED %s", c->account.username); resume_timer(s, g);
    } else if (!strcmp(command, "DRAW")) {
        if (g->draw_offered_by != -1) { emit(s, ci, "ERROR Draw offer already pending"); return; }
        freeze_timer(s, g);
        if (!record_event(s, index, "DRAW_OFFER %d", p)) return;
        g->draw_offered_by = p;
        emit(s, ci, "DRAW_OFFER_SENT");
        emit(s, g->client_indices[1-p], "DRAW_OFFER %s", c->account.username);
    } else {
        if (g->draw_offered_by == -1 || g->draw_offered_by == p) { emit(s, ci, "ERROR No draw offer from opponent"); return; }
        if (!strcmp(command, "ACCEPT_DRAW")) { finish_match(s, index, -1, "DRAW_ACCEPTED"); return; }
        if (!record_event(s, index, "DRAW_DECLINED %d", p)) return;
        g->draw_offered_by = -1;
        emit_both(s, g, "DRAW_DECLINED %s", c->account.username); resume_timer(s, g);
    }
}
void process_deadlines(struct Server *s)
{
    int64_t now = server_now(s);
    for (int i = 0; i < MAX_GAMES; ++i) {
        struct GameSession *g = &s->games[i];
        if (!g->active) continue;
        if (!g->playing && now >= g->placement_deadline) {
            for (int p = 0; p < 2 && g->active; ++p) {
                if (all_ships_placed(g->game.players[p].fleet)) continue;
                struct Player fleet;
                if (!generate_random_fleet(&fleet, s->random, s->random_context)) { abort_match(s, i); break; }
                accept_fleet(s, i, p, &fleet, 1);
            }
        } else if (g->playing && !g->paused && g->draw_offered_by == -1 && now >= g->turn_deadline) {
            int p = g->game.current_turn;
            if (!record_event(s, i, "TIMEOUT %d", p)) continue;
            emit_both(s, g, "TURN_TIMEOUT %s", s->clients[g->client_indices[p]].account.username);
            g->game.current_turn = 1-p;
            g->turn_deadline = server_now(s) + TURN_MS;
            announce_turn(s, g);
        }
    }
}
int64_t next_wakeup_ms(struct Server *s)
{
    int64_t now = server_now(s), wait = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) if (s->clients[i].ready) { wait = 1000; break; }
    for (int i = 0; i < MAX_GAMES; ++i) {
        struct GameSession *g = &s->games[i];
        if (!g->active || (g->playing && (g->paused || g->draw_offered_by != -1))) continue;
        int64_t left = (g->playing ? g->turn_deadline : g->placement_deadline) - now;
        if (left < 0) left = 0;
        if (wait < 0 || left < wait) wait = left;
    }
    return wait;
}
