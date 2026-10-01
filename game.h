#ifndef GAME_H
#define GAME_H

#define BOARD_SIZE 10
#define FLEET_SIZE 5
#define PLAYER_COUNT 2
#define NO_WINNER -1
#define EMPTY_CELL '.'
#define SHIP_CELL 'S'
#define HIT_CELL 'X'
#define MISS_CELL 'O'

#define SHOT_OUT_OF_BOUNDS -1
#define SHOT_ALREADY_TAKEN -2
#define SHOT_MISS 0
#define SHOT_HIT 1

#define PLAY_GAME_OVER -3
#define PLAY_NOT_READY -4
#define PLAY_INVALID_GAME -5

#define VERTICAL 0
#define HORIZONTAL 1

struct Ship
{
    char name[20];
    int length;
    int row;
    int column;
    int direction;
    int placed;
};

struct Player
{
    char board[BOARD_SIZE][BOARD_SIZE];
    struct Ship fleet[FLEET_SIZE];
};

struct Game
{
    struct Player players[PLAYER_COUNT];
    int current_turn;
    int game_over;
    int winner;
};

void initialize_board(char board[BOARD_SIZE][BOARD_SIZE]);
void initialize_fleet(struct Ship fleet[FLEET_SIZE]);
void initialize_player(struct Player *player);
void initialize_game(struct Game *game);
void print_board(const char board[BOARD_SIZE][BOARD_SIZE]);
void create_opponent_view(
    const char opponent_board[BOARD_SIZE][BOARD_SIZE],
    char view[BOARD_SIZE][BOARD_SIZE]);
int parse_coordinate(const char *text, int *row, int *column);
int parse_direction(const char *text, int *direction);
int all_ships_placed(const struct Ship fleet[FLEET_SIZE]);
int game_is_ready(const struct Game *game);
int can_place_ship(const char board[BOARD_SIZE][BOARD_SIZE],
                   const struct Ship *ship,
                   int row,
                   int column,
                   int direction);
int place_ship(char board[BOARD_SIZE][BOARD_SIZE],
               struct Ship *ship,
               int row,
               int column,
               int direction);
int shoot(char board[BOARD_SIZE][BOARD_SIZE], int row, int column);
int is_ship_sunk(const char board[BOARD_SIZE][BOARD_SIZE],
                 const struct Ship *ship);
int all_ships_sunk(const char board[BOARD_SIZE][BOARD_SIZE],
                   const struct Ship fleet[FLEET_SIZE]);
int play_turn(struct Game *game, int row, int column);

#endif
