#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "account.h"
#include "storage.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static int scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt = NULL;
    CHECK(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    int n = sqlite3_column_int(stmt, 0); sqlite3_finalize(stmt); return n;
}
int main(void)
{
    char path[] = "/tmp/battleship-accounts-XXXXXX";
    int fd = mkstemp(path); CHECK(fd >= 0); close(fd);
    sqlite3 *db = NULL;
    CHECK(storage_open(&db, path));
    CHECK(valid_username("player_1") && !valid_username("bad name"));
    CHECK(valid_password("secret123") && !valid_password("bad password"));
    CHECK(register_account(db, "alice", "secret") == 1);
    CHECK(register_account(db, "bob", "secret") == 1);
    CHECK(register_account(db, "alice", "other") == 0);
    struct Account a, b;
    CHECK(authenticate_account(db, "alice", "wrong", &a) == 0);
    CHECK(authenticate_account(db, "alice", "secret", &a) == 1);
    CHECK(authenticate_account(db, "bob", "secret", &b) == 1);
    CHECK(a.rating == 1000);
    CHECK(scalar(db, "SELECT COUNT(*) FROM users WHERE length(salt)=16 AND length(password_hash)=32") == 2);
    CHECK(scalar(db, "SELECT COUNT(DISTINCT salt) FROM users") == 2);
    CHECK(scalar(db, "SELECT COUNT(DISTINCT password_hash) FROM users") == 2);
    CHECK(elo_delta(1000,1000,1) == 16 && elo_delta(1000,1000,0) == -16 && elo_delta(1000,1000,0.5) == 0);
    CHECK(elo_delta(1000,1200,1) > 16 && elo_delta(1200,1000,1) < 16);
    int ratings[2] = {1000,1000}, next[2], delta[2];
    sqlite3_int64 id = storage_start(db,a.id,b.id,"MATCHMAKING",ratings);
    CHECK(id > 0 && storage_finish(db,id,0,"RESIGN",200,next,delta));
    CHECK(next[0] == 1016 && next[1] == 984 && delta[0] == 16 && delta[1] == -16);
    CHECK(!storage_finish(db,id,0,"RESIGN",200,next,delta));
    CHECK(scalar(db,"SELECT rating FROM users WHERE username='alice'") == 1016);
    CHECK(scalar(db,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'RESULT %'") == 1);
    ratings[0]=1016; ratings[1]=984;
    id=storage_start(db,a.id,b.id,"CHALLENGE",ratings);
    CHECK(storage_finish(db,id,1,"ALL_SHIPS_SUNK",200,next,delta));
    CHECK(next[0]==1016 && next[1]==984 && delta[0]==0);
    id=storage_start(db,a.id,b.id,"MATCHMAKING",ratings);
    CHECK(sqlite3_exec(db,"CREATE TRIGGER fail_second BEFORE UPDATE ON users WHEN OLD.username='bob' BEGIN SELECT RAISE(FAIL,'injected'); END",NULL,NULL,NULL)==SQLITE_OK);
    CHECK(!storage_finish(db,id,0,"RESIGN",200,next,delta));
    CHECK(scalar(db,"SELECT rating FROM users WHERE username='alice'")==1016);
    CHECK(scalar(db,"SELECT COUNT(*) FROM matches WHERE status='ACTIVE'")==1);
    CHECK(sqlite3_exec(db,"DROP TRIGGER fail_second",NULL,NULL,NULL)==SQLITE_OK);
    CHECK(storage_finish(db,id,-1,"DRAW_ACCEPTED",200,next,delta));
    CHECK(next[0]<1016 && next[1]>984);
    ratings[0]=next[0]; ratings[1]=next[1];
    CHECK(storage_start(db,a.id,b.id,"MATCHMAKING",ratings)>0);
    sqlite3_close(db); db=NULL;
    CHECK(storage_open(&db,path));
    CHECK(authenticate_account(db,"alice","secret",&a)==1 && a.rating==ratings[0]);
    CHECK(scalar(db,"SELECT COUNT(*) FROM matches WHERE status='ACTIVE'")==0);
    CHECK(scalar(db,"SELECT COUNT(*) FROM matches WHERE status='ABORTED' AND reason='SERVER_RESTART'")==1);
    CHECK(scalar(db,"SELECT COUNT(*) FROM matches WHERE status='FINISHED'")==3);
    sqlite3_close(db); unlink(path);
    puts("PASS: salted accounts, Elo, atomic finalization, persistence and recovery");
    return 0;
}
