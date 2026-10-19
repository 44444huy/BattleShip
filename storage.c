#include <math.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"

static int execute(sqlite3 *db, const char *sql)
{
    char *error = NULL;
    int ok = sqlite3_exec(db, sql, NULL, NULL, &error) == SQLITE_OK;
    if (!ok) fprintf(stderr, "Database: %s\n", error ? error : sqlite3_errmsg(db));
    sqlite3_free(error);
    return ok;
}
int storage_open(sqlite3 **db, const char *path)
{
    if (sqlite3_open(path, db) != SQLITE_OK) return 0;
    /* Fail promptly rather than stall all game clocks behind an external writer. */
    sqlite3_busy_timeout(*db, 0);
    if (!execute(*db, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;")) return 0;
    return execute(*db,
        "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY,username TEXT NOT NULL UNIQUE,salt BLOB NOT NULL,"
        "password_hash BLOB NOT NULL,iterations INTEGER NOT NULL,rating INTEGER NOT NULL DEFAULT 1000);"
        "CREATE TABLE IF NOT EXISTS matches("
        "id INTEGER PRIMARY KEY,player0 INTEGER NOT NULL REFERENCES users(id),"
        "player1 INTEGER NOT NULL REFERENCES users(id),source TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'ACTIVE',started_at INTEGER NOT NULL DEFAULT(CAST(strftime('%s','now') AS INTEGER)),"
        "ended_at INTEGER,winner INTEGER,reason TEXT,"
        "rating0_before INTEGER NOT NULL,rating1_before INTEGER NOT NULL,"
        "rating0_after INTEGER,rating1_after INTEGER);"
        "CREATE TABLE IF NOT EXISTS match_events("
        "match_id INTEGER NOT NULL REFERENCES matches(id),seq INTEGER NOT NULL,"
        "elapsed_ms INTEGER NOT NULL,payload TEXT NOT NULL,PRIMARY KEY(match_id,seq));"
        "CREATE INDEX IF NOT EXISTS matches_player0 ON matches(player0,id);"
        "CREATE INDEX IF NOT EXISTS matches_player1 ON matches(player1,id);"
        "UPDATE matches SET status='ABORTED',reason='SERVER_RESTART',ended_at=CAST(strftime('%s','now') AS INTEGER) WHERE status='ACTIVE';");
}
int storage_begin(sqlite3 *db) { return execute(db, "BEGIN IMMEDIATE"); }
int storage_commit(sqlite3 *db) { return execute(db, "COMMIT"); }
void storage_rollback(sqlite3 *db) { sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL); }
int storage_event(sqlite3 *db, sqlite3_int64 match, int64_t elapsed, const char *payload)
{
    sqlite3_stmt *stmt = NULL;
    int ok = sqlite3_prepare_v2(db, "INSERT INTO match_events SELECT ?,COALESCE(MAX(seq),0)+1,?,? FROM match_events WHERE match_id=?", -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, match); sqlite3_bind_int64(stmt, 2, elapsed);
        sqlite3_bind_text(stmt, 3, payload, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, match);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    return ok;
}
sqlite3_int64 storage_start(sqlite3 *db, sqlite3_int64 a, sqlite3_int64 b,
                            const char *source, const int ratings[2])
{
    if (!storage_begin(db)) return 0;
    sqlite3_stmt *stmt = NULL;
    int ok = sqlite3_prepare_v2(db, "INSERT INTO matches(player0,player1,source,rating0_before,rating1_before) VALUES(?,?,?,?,?)", -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, a); sqlite3_bind_int64(stmt, 2, b);
        sqlite3_bind_text(stmt, 3, source, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, ratings[0]); sqlite3_bind_int(stmt, 5, ratings[1]);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    sqlite3_int64 id = sqlite3_last_insert_rowid(db);
    if (ok) ok = storage_event(db, id, 0, "MATCH_START");
    if (ok) ok = storage_commit(db);
    if (!ok) { storage_rollback(db); return 0; }
    return id;
}
int elo_delta(int first, int second, double score)
{
    double expected = 1.0 / (1.0 + pow(10.0, ((double)second - first) / 400.0));
    return (int)lround(32.0 * (score - expected));
}
int storage_finish(sqlite3 *db, sqlite3_int64 match, int winner, const char *reason,
                   int64_t elapsed, int ratings[2], int deltas[2])
{
    if (winner < -1 || winner > 1 || !storage_begin(db)) return 0;
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 users[2] = {0};
    int ok = sqlite3_prepare_v2(db, "SELECT player0,player1,source,rating0_before,rating1_before FROM matches WHERE id=? AND status='ACTIVE'", -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int64(stmt, 1, match);
        ok = sqlite3_step(stmt) == SQLITE_ROW;
        if (ok) {
            users[0] = sqlite3_column_int64(stmt, 0); users[1] = sqlite3_column_int64(stmt, 1);
            ratings[0] = sqlite3_column_int(stmt, 3); ratings[1] = sqlite3_column_int(stmt, 4);
            deltas[0] = strcmp((const char *)sqlite3_column_text(stmt, 2), "MATCHMAKING") == 0
                ? elo_delta(ratings[0], ratings[1], winner == -1 ? 0.5 : winner == 0 ? 1.0 : 0.0) : 0;
            deltas[1] = -deltas[0];
            ratings[0] += deltas[0]; ratings[1] += deltas[1];
        }
    }
    sqlite3_finalize(stmt); stmt = NULL;
    for (int i = 0; ok && i < 2; ++i) {
        ok = sqlite3_prepare_v2(db, "UPDATE users SET rating=? WHERE id=?", -1, &stmt, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int(stmt, 1, ratings[i]); sqlite3_bind_int64(stmt, 2, users[i]);
            ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
        }
        sqlite3_finalize(stmt); stmt = NULL;
    }
    if (ok) ok = sqlite3_prepare_v2(db, "UPDATE matches SET status='FINISHED',winner=?,reason=?,ended_at=CAST(strftime('%s','now') AS INTEGER),rating0_after=?,rating1_after=? WHERE id=? AND status='ACTIVE'", -1, &stmt, NULL) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(stmt, 1, winner); sqlite3_bind_text(stmt, 2, reason, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, ratings[0]); sqlite3_bind_int(stmt, 4, ratings[1]);
        sqlite3_bind_int64(stmt, 5, match);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
    }
    sqlite3_finalize(stmt);
    char event[128]; snprintf(event, sizeof(event), "RESULT %d %s %d %d", winner, reason, deltas[0], deltas[1]);
    if (ok) ok = storage_event(db, match, elapsed, event);
    if (ok) ok = storage_commit(db);
    if (!ok) storage_rollback(db);
    return ok;
}
void storage_abort(sqlite3 *db, sqlite3_int64 match, const char *reason)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "UPDATE matches SET status='ABORTED',reason=?,ended_at=CAST(strftime('%s','now') AS INTEGER) WHERE id=? AND status='ACTIVE'", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, reason, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, match); sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}
