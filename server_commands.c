#include <stdio.h>
#include <string.h>
#include "server.h"

static int available(struct Server *s, int ci)
{
    struct Client *c = &s->clients[ci];
    return c->logged_in && c->fd >= 0 && !c->closing && !c->failed && c->game_index == -1;
}
static void lists(struct Server *s, int ci, const char *command)
{
    int online = !strcmp(command, "LIST_ONLINE"), ready = !strcmp(command, "LIST_READY");
    const char *prefix = online ? "ONLINE" : ready ? "READY" : "CHALLENGE";
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client *c = &s->clients[i];
        if (c->fd >= 0 && c->logged_in && !c->failed && !c->closing &&
            (online || (ready ? c->ready : s->clients[ci].challenges_from[i]))) ++count;
    }
    emit(s, ci, "%s_LIST %d", prefix, count);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client *c = &s->clients[i];
        if (c->fd < 0 || !c->logged_in || c->failed || c->closing) continue;
        if (online) emit(s, ci, "ONLINE_PLAYER %s %s", c->account.username, c->game_index >= 0 ? "IN_GAME" : c->ready ? "READY" : "ONLINE");
        else if (ready && c->ready) emit(s, ci, "READY_PLAYER %s", c->account.username);
        else if (!ready && s->clients[ci].challenges_from[i]) emit(s, ci, "CHALLENGER %s", c->account.username);
    }
    emit(s, ci, "%s_LIST_END", prefix);
}
static void challenge(struct Server *s, int ci, const char *command, const char *username)
{
    struct Client *c = &s->clients[ci];
    int target = find_online_client(s, username);
    if (!available(s, ci)) { emit(s, ci, "ERROR Already in a match"); return; }
    if (target < 0 || target == ci || !available(s, target)) { emit(s, ci, "ERROR Player is not available"); return; }
    struct Client *other = &s->clients[target];
    if (!strcmp(command, "CHALLENGE")) {
        if (c->challenges_from[target] || other->challenges_from[ci]) { emit(s, ci, "ERROR Challenge already pending"); return; }
        other->challenges_from[ci] = 1;
        c->ready = 0;
        emit(s, target, "CHALLENGE_FROM %s", c->account.username);
        emit(s, ci, "CHALLENGE_SENT %s", username);
    } else {
        if (!c->challenges_from[target]) { emit(s, ci, "ERROR No matching challenge"); return; }
        if (!strcmp(command, "ACCEPT_CHALLENGE")) {
            /* Remove the accepted invitation before cancelling unrelated ones. */
            c->challenges_from[target] = 0;
            if (!start_match(s, target, ci, CHALLENGE_MATCH)) c->challenges_from[target] = 1;
        } else {
            c->challenges_from[target] = 0;
            emit(s, target, "CHALLENGE_DECLINED %s", c->account.username);
            emit(s, ci, "DECLINE_CHALLENGE_OK %s", username);
        }
    }
}
static void rematch(struct Server *s, int ci, const char *command)
{
    struct Client *c = &s->clients[ci];
    int other_index = c->last_opponent_index;
    if (!available(s, ci) || other_index < 0 || !available(s, other_index) ||
        s->clients[other_index].last_opponent_index != ci) { emit(s, ci, "ERROR No previous opponent available"); return; }
    struct Client *other = &s->clients[other_index];
    if (!strcmp(command, "REMATCH")) {
        if (other->rematch_from != -1 || c->rematch_from != -1) { emit(s, ci, "ERROR Rematch request already pending"); return; }
        c->ready = 0; other->rematch_from = ci;
        emit(s, other_index, "REMATCH_REQUEST %s", c->account.username);
        emit(s, ci, "REMATCH_SENT %s", other->account.username);
    } else {
        if (c->rematch_from != other_index) { emit(s, ci, "ERROR No rematch request"); return; }
        c->rematch_from = -1;
        if (!strcmp(command, "ACCEPT_REMATCH")) {
            if (!start_match(s, other_index, ci, REMATCH_MATCH)) c->rematch_from = other_index;
        } else {
            emit(s, other_index, "REMATCH_DECLINED %s", c->account.username);
            emit(s, ci, "DECLINE_REMATCH_OK %s", other->account.username);
        }
    }
}
void process_message(struct Server *s, int ci, const char *message)
{
    /* Includes calls from a coalesced TCP read: deadlines win at the boundary. */
    process_deadlines(s);
    struct Client *c = &s->clients[ci];
    if (c->closing || c->failed) return;
    char copy[MESSAGE_SIZE];
    if (strlen(message) >= sizeof(copy)) { emit(s, ci, "ERROR Message too long"); return; }
    strcpy(copy, message);
    char *argv[32], *save = NULL;
    int argc = 0;
    for (char *word = strtok_r(copy, " \t", &save); word; word = strtok_r(NULL, " \t", &save)) {
        if (argc == 32) { emit(s, ci, "ERROR Too many arguments"); return; }
        argv[argc++] = word;
    }
    if (!argc || strlen(argv[0]) > 31) { emit(s, ci, "ERROR Unknown command"); return; }
    const char *command = argv[0];
    if (!strcmp(command, "HELLO") && argc == 1) { emit(s, ci, "WELCOME"); return; }
    if (!strcmp(command, "PING") && argc == 1) { emit(s, ci, "PONG"); return; }
    if (!strcmp(command, "QUIT") && argc == 1) {
        logout_client(s, ci, "DISCONNECT"); emit(s, ci, "BYE"); c->closing = 1; return;
    }
    if (!strcmp(command, "REGISTER") || !strcmp(command, "LOGIN")) {
        if (c->logged_in) { emit(s, ci, "ERROR Already logged in"); return; }
        if (argc != 3 || !valid_username(argv[1]) || !valid_password(argv[2])) { emit(s, ci, "ERROR Invalid username or password format"); return; }
        if (!strcmp(command, "REGISTER")) {
            int result = register_account(s->db, argv[1], argv[2]);
            emit(s, ci, "%s", result == 1 ? "REGISTER_OK" : result == 0 ? "ERROR Username already exists" : "ERROR Storage unavailable");
        } else {
            if (find_online_client(s, argv[1]) != -1) { emit(s, ci, "ERROR Account already online"); return; }
            struct Account account;
            int result = authenticate_account(s->db, argv[1], argv[2], &account);
            if (result == 1) {
                c->account = account; c->logged_in = 1;
                emit(s, ci, "LOGIN_OK %s", account.username);
            } else emit(s, ci, "%s", result == 0 ? "ERROR Invalid username or password" : "ERROR Storage unavailable");
        }
        return;
    }
    if (!c->logged_in) { emit(s, ci, "ERROR Login required"); return; }
    if (!strcmp(command, "STATS") || !strcmp(command, "HISTORY") || !strcmp(command, "REPLAY") || !strcmp(command, "LEADERBOARD")) {
        send_query(s, ci, command, argc, argv); return;
    }
    if (!strcmp(command, "CHALLENGE") || !strcmp(command, "ACCEPT_CHALLENGE") || !strcmp(command, "DECLINE_CHALLENGE")) {
        if (argc != 2) emit(s, ci, "ERROR Use: %s <username>", command);
        else challenge(s, ci, command, argv[1]);
        return;
    }
    if (!strcmp(command, "PLACE_FLEET") || !strcmp(command, "SHOOT") ||
        !strcmp(command, "SHOW_BOARD") || !strcmp(command, "SHOW_OPPONENT") ||
        !strcmp(command, "RESIGN") || !strcmp(command, "PAUSE") || !strcmp(command, "RESUME") ||
        !strcmp(command, "DRAW") || !strcmp(command, "ACCEPT_DRAW") || !strcmp(command, "DECLINE_DRAW")) {
        handle_game_command(s, ci, command, argc, argv); return;
    }
    if (argc != 1) { emit(s, ci, "ERROR Unexpected arguments"); return; }
    if (!strcmp(command, "LOGOUT")) {
        logout_client(s, ci, "LOGOUT"); emit(s, ci, "LOGOUT_OK");
    } else if (!strcmp(command, "READY") || !strcmp(command, "UNREADY")) {
        if (!available(s, ci)) { emit(s, ci, "ERROR Already in a match"); return; }
        if (!strcmp(command, "READY")) {
            if (!c->ready) { c->queued_at = server_now(s); c->queue_order = ++s->queue_order; c->ready = 1; }
            emit(s, ci, "READY_OK");
        } else { c->ready = 0; emit(s, ci, "UNREADY_OK"); }
    } else if (!strcmp(command, "LIST_READY") || !strcmp(command, "LIST_ONLINE") || !strcmp(command, "LIST_CHALLENGES")) {
        lists(s, ci, command);
    } else if (!strcmp(command, "REMATCH") || !strcmp(command, "ACCEPT_REMATCH") || !strcmp(command, "DECLINE_REMATCH")) {
        rematch(s, ci, command);
    } else emit(s, ci, "ERROR Unknown command");
}
