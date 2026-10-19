#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"

static int number(const char *text, sqlite3_int64 *value)
{
    if (!text || !*text) return 0;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return 0;
    char *end; errno = 0;
    long long n = strtoll(text, &end, 10);
    if (errno || *end || n < 0) return 0;
    *value = n; return 1;
}
static const char *text(sqlite3_stmt *stmt, int column)
{
    const unsigned char *value = sqlite3_column_text(stmt, column);
    return value ? (const char *)value : "-";
}
void send_query(struct Server *s, int ci, const char *command, int argc, char **argv)
{
    struct Client *client = &s->clients[ci];
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 offset = 0, match = 0;
    char rows[100][MESSAGE_SIZE], header[MESSAGE_SIZE];
    int count = 0, more = 0, rc = SQLITE_ERROR;
    sqlite3_int64 next = -1;
    if (!strcmp(command, "STATS")) {
        if (argc != 1) goto usage;
        const char *sql = "SELECT u.rating,COALESCE(SUM(m.winner=CASE WHEN m.player0=u.id THEN 0 ELSE 1 END),0),"
            "COALESCE(SUM(m.winner>=0 AND m.winner!=CASE WHEN m.player0=u.id THEN 0 ELSE 1 END),0),"
            "COALESCE(SUM(m.winner=-1),0) FROM users u LEFT JOIN matches m ON (m.player0=u.id OR m.player1=u.id) AND m.status='FINISHED' WHERE u.id=? GROUP BY u.id";
        if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) goto failure;
        sqlite3_bind_int64(stmt, 1, client->account.id);
        if (sqlite3_step(stmt) != SQLITE_ROW) goto failure;
        snprintf(header, sizeof(header), "STATS %d %d %d %d", sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2), sqlite3_column_int(stmt, 3));
        if (sqlite3_step(stmt) != SQLITE_DONE) goto failure;
        sqlite3_finalize(stmt); emit(s, ci, "%s", header); return;
    }
    if (!strcmp(command, "REPLAY")) {
        if ((argc != 2 && argc != 3) || !number(argv[1], &match) || match == 0 ||
            (argc == 3 && !number(argv[2], &offset))) goto usage;
        const char *sql = "SELECT a.username,b.username,m.source,m.winner,m.reason FROM matches m JOIN users a ON a.id=m.player0 JOIN users b ON b.id=m.player1 WHERE m.id=? AND m.status='FINISHED' AND (m.player0=? OR m.player1=?)";
        if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) goto failure;
        sqlite3_bind_int64(stmt, 1, match); sqlite3_bind_int64(stmt, 2, client->account.id); sqlite3_bind_int64(stmt, 3, client->account.id);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) { sqlite3_finalize(stmt); emit(s, ci, "ERROR Replay unavailable"); return; }
        if (rc != SQLITE_ROW) goto failure;
        snprintf(header, sizeof(header), "REPLAY_BEGIN %lld %s %s %s %d %s", match, text(stmt, 0), text(stmt, 1), text(stmt, 2), sqlite3_column_int(stmt, 3), text(stmt, 4));
        sqlite3_finalize(stmt); stmt = NULL;
        if (sqlite3_prepare_v2(s->db, "SELECT seq,elapsed_ms,payload FROM match_events WHERE match_id=? AND seq>? ORDER BY seq LIMIT 101", -1, &stmt, NULL) != SQLITE_OK) goto failure;
        sqlite3_bind_int64(stmt, 1, match); sqlite3_bind_int64(stmt, 2, offset);
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (count == 100) { more = 1; break; }
            next = sqlite3_column_int64(stmt, 0);
            int length = snprintf(rows[count], MESSAGE_SIZE, "REPLAY_EVENT %lld %lld %s", next, sqlite3_column_int64(stmt, 1), text(stmt, 2));
            if (length < 0 || length >= MESSAGE_SIZE) goto failure;
            ++count;
        }
        if (rc != SQLITE_DONE && !more) goto failure;
        sqlite3_finalize(stmt);
        emit(s, ci, "%s", header);
        for (int i = 0; i < count; ++i) emit(s, ci, "%s", rows[i]);
        emit(s, ci, "REPLAY_END %lld", more ? next : (sqlite3_int64)-1); return;
    }
    if (argc != 2 || !number(argv[1], &offset) || offset > INT_MAX-20) goto usage;
    if (!strcmp(command, "LEADERBOARD")) {
        if (sqlite3_prepare_v2(s->db, "SELECT username,rating FROM users ORDER BY rating DESC,username LIMIT 21 OFFSET ?", -1, &stmt, NULL) != SQLITE_OK) goto failure;
        sqlite3_bind_int64(stmt, 1, offset);
    } else {
        const char *sql = "SELECT m.id,m.started_at,u.username,m.source,"
            "CASE WHEN m.winner=-1 THEN 'DRAW' WHEN m.winner=CASE WHEN m.player0=? THEN 0 ELSE 1 END THEN 'WIN' ELSE 'LOSE' END,"
            "m.reason,CASE WHEN m.player0=? THEN m.rating0_after-m.rating0_before ELSE m.rating1_after-m.rating1_before END "
            "FROM matches m JOIN users u ON u.id=CASE WHEN m.player0=? THEN m.player1 ELSE m.player0 END "
            "WHERE m.status='FINISHED' AND (m.player0=? OR m.player1=?) ORDER BY m.id DESC LIMIT 21 OFFSET ?";
        if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) goto failure;
        for (int i = 1; i <= 5; ++i) sqlite3_bind_int64(stmt, i, client->account.id);
        sqlite3_bind_int64(stmt, 6, offset);
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count == 20) { more = 1; break; }
        if (!strcmp(command, "LEADERBOARD"))
            snprintf(rows[count], MESSAGE_SIZE, "RANK %lld %s %d", offset+count+1, text(stmt, 0), sqlite3_column_int(stmt, 1));
        else snprintf(rows[count], MESSAGE_SIZE, "HISTORY_ITEM %lld %lld %s %s %s %s %d", sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 1), text(stmt, 2), text(stmt, 3), text(stmt, 4), text(stmt, 5), sqlite3_column_int(stmt, 6));
        ++count;
    }
    if (rc != SQLITE_DONE && !more) goto failure;
    sqlite3_finalize(stmt);
    emit(s, ci, "%s_BEGIN %d", command, count);
    for (int i = 0; i < count; ++i) emit(s, ci, "%s", rows[i]);
    emit(s, ci, "%s_END %lld", command, more ? offset+count : (sqlite3_int64)-1); return;
usage:
    emit(s, ci, "ERROR Invalid %s arguments", command); return;
failure:
    sqlite3_finalize(stmt); emit(s, ci, "ERROR Storage unavailable");
}
