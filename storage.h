#ifndef STORAGE_H
#define STORAGE_H
#include <sqlite3.h>
#include <stdint.h>
int storage_open(sqlite3 **db, const char *path);
int storage_begin(sqlite3 *db);
int storage_commit(sqlite3 *db);
void storage_rollback(sqlite3 *db);
int storage_event(sqlite3 *db, sqlite3_int64 match, int64_t elapsed, const char *payload);
sqlite3_int64 storage_start(sqlite3 *db, sqlite3_int64 a, sqlite3_int64 b, const char *source, const int ratings[2]);
int storage_finish(sqlite3 *db, sqlite3_int64 match, int winner, const char *reason,
                   int64_t elapsed, int ratings[2], int deltas[2]);
void storage_abort(sqlite3 *db, sqlite3_int64 match, const char *reason);
int elo_delta(int first, int second, double score);
#endif
