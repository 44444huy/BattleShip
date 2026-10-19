#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static int64_t now;
static uint32_t seed;
static int peers[6];
static int64_t clock_value(void *context) { (void)context; return now; }
static int random_value(void *context, uint32_t *value)
{
    uint32_t *state=context;
    *state ^= *state<<13; *state ^= *state>>17; *state ^= *state<<5; *value=*state; return 1;
}
static struct Server *fixture(void)
{
    struct Server *s=malloc(sizeof(*s)); CHECK(s);
    sqlite3 *db=NULL; CHECK(storage_open(&db,":memory:"));
    initialize_server(s,db); now=1000; seed=123456;
    s->clock=clock_value; s->random=random_value; s->random_context=&seed;
    for (int i=0;i<6;i++) {
        int pair[2]; CHECK(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
        s->clients[i].fd=pair[0]; peers[i]=pair[1];
        char name[16]; snprintf(name,sizeof(name),"player%d",i);
        CHECK(register_account(db,name,"secret")==1);
        CHECK(authenticate_account(db,name,"secret",&s->clients[i].account)==1);
        s->clients[i].logged_in=1;
    }
    return s;
}
static void destroy(struct Server *s)
{
    s->shutting_down=1;
    for(int i=0;i<6;i++) { remove_client(s,i); close(peers[i]); }
    sqlite3_close(s->db); free(s);
}
static void clear_output(struct Server *s)
{
    for(int i=0;i<6;i++) s->clients[i].output_used=0;
}
static int contains(struct Server *s,int ci,const char *text)
{
    CHECK(s->clients[ci].output_used < OUTPUT_CAPACITY);
    s->clients[ci].output[s->clients[ci].output_used]='\0';
    return strstr(s->clients[ci].output,text)!=NULL;
}
static int scalar(struct Server *s,const char *sql)
{
    sqlite3_stmt *stmt=NULL;
    CHECK(sqlite3_prepare_v2(s->db,sql,-1,&stmt,NULL)==SQLITE_OK);
    CHECK(sqlite3_step(stmt)==SQLITE_ROW);
    int value=sqlite3_column_int(stmt,0); sqlite3_finalize(stmt); return value;
}
static void rating(struct Server *s,int ci,int value)
{
    s->clients[ci].account.rating=value;
    char sql[128]; snprintf(sql,sizeof(sql),"UPDATE users SET rating=%d WHERE id=%lld",value,s->clients[ci].account.id);
    CHECK(sqlite3_exec(s->db,sql,NULL,NULL,NULL)==SQLITE_OK);
}
static const char *fleet="PLACE_FLEET 1 A1 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H";
static void manual_game(struct Server *s,enum MatchSource source)
{
    CHECK(start_match(s,0,1,source));
    process_message(s,0,fleet); process_message(s,1,fleet);
    CHECK(s->games[s->clients[0].game_index].playing);
    clear_output(s);
}
static void test_queue(void)
{
    CHECK(matchmaking_gap(9999)==100 && matchmaking_gap(10000)==150 && matchmaking_gap(19999)==150);
    CHECK(matchmaking_gap(20000)==200 && matchmaking_gap(99999999)==200);
    struct Server *s=fixture(); rating(s,1,1100);
    process_message(s,0,"READY"); process_message(s,1,"READY"); run_matchmaking(s);
    CHECK(s->clients[0].game_index>=0); destroy(s);
    s=fixture(); rating(s,1,1201);
    process_message(s,0,"READY"); int64_t entered=s->clients[0].queued_at;
    uint64_t order=s->clients[0].queue_order;
    now+=10000; process_message(s,0,"READY");
    CHECK(s->clients[0].queued_at==entered && s->clients[0].queue_order==order);
    process_message(s,1,"READY"); now+=900000; run_matchmaking(s);
    CHECK(s->clients[0].ready && s->clients[1].ready && s->clients[0].game_index==-1);
    rating(s,1,1200); run_matchmaking(s);
    CHECK(s->clients[0].game_index>=0 && s->clients[0].game_index==s->clients[1].game_index);
    CHECK(!s->clients[0].ready && !s->clients[1].ready);
    CHECK(contains(s,0,"MATCH_FOUND player1\nMATCH_START\n"));
    run_matchmaking(s); CHECK(scalar(s,"SELECT COUNT(*) FROM matches")==1);
    destroy(s);

    s=fixture(); rating(s,1,1150);
    process_message(s,0,"READY"); process_message(s,1,"READY"); now+=9999; run_matchmaking(s);
    CHECK(s->clients[0].game_index==-1); now++; run_matchmaking(s); CHECK(s->clients[0].game_index>=0);
    destroy(s);

    s=fixture(); rating(s,1,1100); rating(s,2,1050); rating(s,3,1050);
    process_message(s,0,"READY"); process_message(s,2,"READY"); process_message(s,3,"READY"); process_message(s,1,"READY");
    run_matchmaking(s);
    CHECK(s->clients[0].game_index==s->clients[2].game_index);
    CHECK(s->clients[1].game_index==s->clients[3].game_index);
    CHECK(s->clients[0].game_index!=s->clients[1].game_index);
    destroy(s);
}
static void test_challenges(void)
{
    struct Server *s=fixture();
    process_message(s,0,"READY"); process_message(s,0,"CHALLENGE player1");
    CHECK(!s->clients[0].ready && s->clients[1].challenges_from[0]);
    process_message(s,1,"UNREADY"); CHECK(s->clients[1].challenges_from[0]);
    process_message(s,1,"DECLINE_CHALLENGE player0"); CHECK(!s->clients[1].challenges_from[0]);
    process_message(s,0,"CHALLENGE player1"); process_message(s,1,"ACCEPT_CHALLENGE player0");
    CHECK(s->clients[0].game_index>=0);
    CHECK(s->games[s->clients[0].game_index].source==CHALLENGE_MATCH);
    process_message(s,0,"RESIGN"); CHECK(s->clients[0].account.rating==1000);
    process_message(s,0,"REMATCH"); process_message(s,1,"ACCEPT_REMATCH");
    CHECK(s->games[s->clients[0].game_index].source==REMATCH_MATCH);
    process_message(s,1,"RESIGN"); CHECK(s->clients[0].account.rating==1000);
    process_message(s,0,"CHALLENGE player1"); process_message(s,1,"READY"); process_message(s,2,"READY");
    clear_output(s); run_matchmaking(s);
    CHECK(contains(s,0,"CHALLENGE_CANCELLED player1"));
    process_message(s,1,"RESIGN"); CHECK(s->clients[1].account.rating==984 && s->clients[2].account.rating==1016);
    destroy(s);
}
static void test_fleets(void)
{
    struct Server *s=fixture(); CHECK(start_match(s,0,1,MATCHMAKING_MATCH));
    int index=s->clients[0].game_index;
    struct GameSession *g=&s->games[index]; int64_t deadline=g->placement_deadline;
    process_message(s,0,"PLACE_FLEET 1 A1 H 2 A1 H 3 E1 H 4 G1 H 5 I1 H");
    CHECK(!all_ships_placed(g->game.players[0].fleet));
    CHECK(scalar(s,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'SHIP %'")==0);
    process_message(s,0,fleet); struct Player accepted=g->game.players[0];
    process_message(s,0,fleet); CHECK(contains(s,0,"ERROR Fleet already accepted"));
    now=deadline-1; process_deadlines(s); CHECK(!g->playing);
    clear_output(s); now=deadline; process_deadlines(s);
    CHECK(g->playing && !memcmp(&accepted,&g->game.players[0],sizeof(accepted)));
    CHECK(contains(s,1,"AUTO_FLEET") && !contains(s,0,"AUTO_FLEET"));
    CHECK(contains(s,0,"GAME_READY") && g->turn_deadline==now+TURN_MS);
    CHECK(scalar(s,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'SHIP % AUTO'")==5);
    CHECK(scalar(s,"SELECT COUNT(*) FROM matches WHERE status='FINISHED'")==0);
    destroy(s);
    s=fixture(); CHECK(start_match(s,0,1,CHALLENGE_MATCH));
    now+=PLACEMENT_MS; process_message(s,0,fleet);
    CHECK(contains(s,0,"AUTO_FLEET") && contains(s,0,"ERROR Fleet already accepted"));
    CHECK(s->games[s->clients[0].game_index].playing); destroy(s);
    for(uint32_t n=1;n<=100;n++) {
        struct Player p; uint32_t state=n;
        CHECK(generate_random_fleet(&p,random_value,&state));
        struct Player rebuilt; initialize_player(&rebuilt);
        for(int j=0;j<FLEET_SIZE;j++) CHECK(place_ship(rebuilt.board,&rebuilt.fleet[j],p.fleet[j].row,p.fleet[j].column,p.fleet[j].direction));
        CHECK(!memcmp(p.board,rebuilt.board,sizeof(p.board)));
    }
}
static void test_timers(void)
{
    struct Server *s=fixture(); manual_game(s,MATCHMAKING_MATCH);
    struct GameSession *g=&s->games[s->clients[0].game_index];
    now+=1000; process_message(s,0,"SHOOT A1"); CHECK(g->game.current_turn==0 && g->turn_deadline==now+TURN_MS);
    int64_t deadline=g->turn_deadline;
    now+=1000; process_message(s,0,"SHOOT A1"); CHECK(g->turn_deadline==deadline);
    process_message(s,0,"SHOOT Z99"); CHECK(g->turn_deadline==deadline);
    process_message(s,0,"SHOOT J10"); CHECK(g->game.current_turn==1 && g->turn_deadline==now+TURN_MS);
    struct Game saved=g->game; now=g->turn_deadline; process_message(s,1,"SHOOT A1");
    CHECK(g->game.current_turn==0 && contains(s,1,"ERROR Not your turn"));
    CHECK(!memcmp(saved.players,g->game.players,sizeof(saved.players)));
    CHECK(contains(s,0,"TURN_TIMEOUT player1"));
    now+=5000; process_message(s,0,"PAUSE"); CHECK(g->paused && g->remaining_ms==25000);
    now+=900000; process_deadlines(s); CHECK(g->game.current_turn==0);
    process_message(s,1,"RESUME"); CHECK(g->paused);
    process_message(s,1,"DRAW"); CHECK(g->draw_offered_by==1);
    process_message(s,0,"RESUME"); CHECK(!g->paused && g->draw_offered_by==1);
    process_message(s,0,"DECLINE_DRAW"); CHECK(g->turn_deadline==now+25000);
    now+=1000; process_message(s,0,"DRAW"); CHECK(g->remaining_ms==24000);
    process_message(s,1,"DRAW"); CHECK(g->draw_offered_by==0);
    now+=800000; process_deadlines(s); CHECK(g->active);
    process_message(s,1,"DECLINE_DRAW"); CHECK(g->turn_deadline==now+24000);
    for(int n=0;n<8;n++) { clear_output(s); now=g->turn_deadline; process_deadlines(s); CHECK(g->active); }
    process_message(s,0,"DRAW"); process_message(s,1,"ACCEPT_DRAW");
    CHECK(!g->active && s->clients[0].account.rating==1000);
    CHECK(scalar(s,"SELECT COUNT(*) FROM matches WHERE reason='DRAW_ACCEPTED'")==1);
    destroy(s);
}
static void test_history_and_failures(void)
{
    struct Server *s=fixture(); manual_game(s,MATCHMAKING_MATCH);
    int index=s->clients[0].game_index;
    sqlite3_int64 id=s->games[index].id;
    process_message(s,0,"SHOOT A1"); process_message(s,0,"RESIGN");
    CHECK(s->clients[0].account.rating==984 && s->clients[1].account.rating==1016);
    CHECK(!finish_match(s,index,1,"RESIGN"));
    clear_output(s); process_message(s,0,"STATS"); CHECK(contains(s,0,"STATS 984 0 1 0"));
    process_message(s,0,"HISTORY 0"); CHECK(contains(s,0,"MATCHMAKING LOSE RESIGN -16"));
    char replay[64]; snprintf(replay,sizeof(replay),"REPLAY %lld",id);
    process_message(s,0,replay); CHECK(contains(s,0,"SHIP 1 1 A1 H 5 MANUAL") && contains(s,0,"SHOT 0 A1 HIT") && contains(s,0,"REPLAY_END -1"));
    process_message(s,2,replay); CHECK(contains(s,2,"ERROR Replay unavailable"));
    process_message(s,0,"LEADERBOARD 0"); CHECK(contains(s,0,"RANK 1 player1 1016"));
    manual_game(s,MATCHMAKING_MATCH); index=s->clients[0].game_index;
    CHECK(sqlite3_exec(s->db,"CREATE TRIGGER fail_shot BEFORE INSERT ON match_events WHEN NEW.payload LIKE 'SHOT %' BEGIN SELECT RAISE(FAIL,'injected'); END",NULL,NULL,NULL)==SQLITE_OK);
    process_message(s,0,"SHOOT A1");
    CHECK(contains(s,0,"MATCH_ABORTED") && !contains(s,0,"SHOT_RESULT"));
    CHECK(!s->games[index].active);
    CHECK(s->clients[0].account.rating==984);
    CHECK(sqlite3_exec(s->db,"DROP TRIGGER fail_shot",NULL,NULL,NULL)==SQLITE_OK);
    manual_game(s,MATCHMAKING_MATCH);
    remove_client(s,0); CHECK(s->clients[1].game_index==-1 && contains(s,1,"GAME_RESULT WIN DISCONNECT"));
    CHECK(scalar(s,"SELECT COUNT(*) FROM matches WHERE reason='DISCONNECT'")==1);
    destroy(s);
}
static void test_sunk_and_win(void)
{
    struct Server *s=fixture(); manual_game(s,MATCHMAKING_MATCH);
    const int lengths[5]={5,4,3,3,2};
    for(int j=0;j<5;j++) for(int col=1;col<=lengths[j];col++) {
        char shot[32]; snprintf(shot,sizeof(shot),"SHOOT %c%d",'A'+j*2,col);
        process_message(s,0,shot);
    }
    CHECK(contains(s,0,"SHIP_SUNK 1 A1 H 5"));
    CHECK(contains(s,0,"GAME_RESULT WIN ALL_SHIPS_SUNK"));
    CHECK(s->clients[0].account.rating==1016);
    CHECK(scalar(s,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'SHOT %'")==17);
    CHECK(scalar(s,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'SUNK %'")==5);
    destroy(s);
}
static void test_pagination_and_validation(void)
{
    struct Server *s=fixture();
    CHECK(start_match(s,0,1,CHALLENGE_MATCH));
    struct GameSession *g=&s->games[s->clients[0].game_index];
    int64_t deadline=g->placement_deadline;
    const char *invalid[]={
        "PLACE_FLEET 1 A1 H 1 C1 H 3 E1 H 4 G1 H 5 I1 H",
        "PLACE_FLEET 1 A8 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H",
        "PLACE_FLEET 1 A1 X 2 C1 H 3 E1 H 4 G1 H 5 I1 H",
        "PLACE_FLEET 1 Z1 H 2 C1 H 3 E1 H 4 G1 H 5 I1 H",
        "PLACE_FLEET 1 A1 H 2 C1 H 3 E1 H 4 G1 H"
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        now++; process_message(s,0,invalid[i]);
        CHECK(!all_ships_placed(g->game.players[0].fleet) && g->placement_deadline==deadline);
    }
    /* A partially inserted fleet transaction rolls back all five placements. */
    CHECK(sqlite3_exec(s->db,"CREATE TRIGGER fail_fleet BEFORE INSERT ON match_events WHEN NEW.payload LIKE 'SHIP 0 3 %' BEGIN SELECT RAISE(FAIL,'injected'); END",NULL,NULL,NULL)==SQLITE_OK);
    clear_output(s); process_message(s,0,fleet);
    CHECK(contains(s,0,"MATCH_ABORTED") && !contains(s,0,"FLEET_READY"));
    CHECK(scalar(s,"SELECT COUNT(*) FROM match_events WHERE payload LIKE 'SHIP %'")==0);
    CHECK(sqlite3_exec(s->db,"DROP TRIGGER fail_fleet",NULL,NULL,NULL)==SQLITE_OK);

    manual_game(s,CHALLENGE_MATCH); g=&s->games[s->clients[0].game_index];
    sqlite3_int64 id=g->id;
    char query[80]; snprintf(query,sizeof(query),"REPLAY %lld",id);
    process_message(s,0,query); CHECK(contains(s,0,"ERROR Replay unavailable"));
    for(int i=0;i<110;i++) { now=g->turn_deadline; process_deadlines(s); clear_output(s); }
    process_message(s,0,"RESIGN");
    clear_output(s); process_message(s,0,query);
    CHECK(contains(s,0,"REPLAY_EVENT 100 ") && contains(s,0,"REPLAY_END 100"));
    CHECK(!contains(s,0,"REPLAY_EVENT 101 "));
    snprintf(query,sizeof(query),"REPLAY %lld 100",id);
    clear_output(s); process_message(s,0,query);
    CHECK(contains(s,0,"REPLAY_EVENT 101 ") && contains(s,0,"REPLAY_END -1"));
    for(int i=0;i<20;i++) {
        CHECK(start_match(s,0,1,CHALLENGE_MATCH));
        process_message(s,0,"RESIGN"); clear_output(s);
    }
    process_message(s,0,"HISTORY 0"); CHECK(contains(s,0,"HISTORY_BEGIN 20") && contains(s,0,"HISTORY_END 20"));
    clear_output(s); process_message(s,0,"HISTORY 20");
    CHECK(contains(s,0,"HISTORY_BEGIN 1") && contains(s,0,"HISTORY_END -1"));
    process_message(s,0,"HISTORY -1"); CHECK(contains(s,0,"ERROR Invalid HISTORY arguments"));
    clear_output(s); process_message(s,0,"LEADERBOARD 0");
    CHECK(contains(s,0,"RANK 1 player0 1000\nRANK 2 player1 1000"));
    destroy(s);
}
int main(void)
{
    test_queue(); test_challenges(); test_fleets(); test_timers(); test_history_and_failures(); test_sunk_and_win();
    test_pagination_and_validation();
    puts("PASS: matchmaking, challenges, fleets, timers, history, ratings and failure handling");
    return 0;
}
