#include <stdio.h>
#include "game.h"

void initialize_board(char board[BOARD_SIZE][BOARD_SIZE])
{
    for (int row = 0; row < BOARD_SIZE; row++)
    {
        for (int column = 0; column < BOARD_SIZE; column++)
        {
            board[row][column] = EMPTY_CELL;
        }
    }
}

void initialize_fleet(struct Ship fleet[FLEET_SIZE])
{
    struct Ship default_fleet[FLEET_SIZE] = {
        {"Carrier", 5, -1, -1, HORIZONTAL, 0},
        {"Battleship", 4, -1, -1, HORIZONTAL, 0},
        {"Cruiser", 3, -1, -1, HORIZONTAL, 0},
        {"Submarine", 3, -1, -1, HORIZONTAL, 0},
        {"Destroyer", 2, -1, -1, HORIZONTAL, 0}};

    for (int i = 0; i < FLEET_SIZE; i++)
    {
        fleet[i] = default_fleet[i];
    }
}

void initialize_player(struct Player *player)
{
    if (player == NULL)
    {
        return;
    }

    initialize_board(player->board);
    initialize_fleet(player->fleet);
}

void initialize_game(struct Game *game)
{
    if (game == NULL)
    {
        return;
    }

    for (int i = 0; i < PLAYER_COUNT; i++)
    {
        initialize_player(&game->players[i]);
    }

    game->current_turn = 0;
    game->game_over = 0;
    game->winner = NO_WINNER;
}

void print_board(const char board[BOARD_SIZE][BOARD_SIZE])
{
    printf("    ");

    for (int column = 1; column <= BOARD_SIZE; column++)
    {
        printf("%2d ", column);
    }

    printf("\n");

    for (int row = 0; row < BOARD_SIZE; row++)
    {
        printf(" %c  ", 'A' + row);

        for (int column = 0; column < BOARD_SIZE; column++)
        {
            printf(" %c ", board[row][column]);
        }

        printf("\n");
    }
}

void create_opponent_view(
    const char opponent_board[BOARD_SIZE][BOARD_SIZE],
    char view[BOARD_SIZE][BOARD_SIZE])
{
    for (int row = 0; row < BOARD_SIZE; row++)
    {
        for (int column = 0; column < BOARD_SIZE; column++)
        {
            if (opponent_board[row][column] == SHIP_CELL)
            {
                view[row][column] = EMPTY_CELL;
            }
            else
            {
                view[row][column] = opponent_board[row][column];
            }
        }
    }
}

int parse_coordinate(const char *text, int *row, int *column)
{
    char row_letter;
    char extra_character;
    int column_number;

    if (text == NULL || row == NULL || column == NULL)
    {
        return 0;
    }

    if (sscanf(text, " %c%d %c",
               &row_letter, &column_number, &extra_character) != 2)
    {
        return 0;
    }

    if (row_letter >= 'a' && row_letter <= 'j')
    {
        row_letter = row_letter - ('a' - 'A');
    }

    if (row_letter < 'A' || row_letter > 'J' ||
        column_number < 1 || column_number > BOARD_SIZE)
    {
        return 0;
    }

    *row = row_letter - 'A';
    *column = column_number - 1;
    return 1;
}

int parse_direction(const char *text, int *direction)
{
    char direction_letter;
    char extra_character;

    if (text == NULL || direction == NULL)
    {
        return 0;
    }

    if (sscanf(text, " %c %c", &direction_letter, &extra_character) != 1)
    {
        return 0;
    }

    if (direction_letter == 'h' || direction_letter == 'H')
    {
        *direction = HORIZONTAL;
        return 1;
    }

    if (direction_letter == 'v' || direction_letter == 'V')
    {
        *direction = VERTICAL;
        return 1;
    }

    return 0;
}

int all_ships_placed(const struct Ship fleet[FLEET_SIZE])
{
    for (int i = 0; i < FLEET_SIZE; i++)
    {
        if (!fleet[i].placed)
        {
            return 0;
        }
    }

    return 1;
}

int game_is_ready(const struct Game *game)
{
    if (game == NULL)
    {
        return 0;
    }

    for (int i = 0; i < PLAYER_COUNT; i++)
    {
        if (!all_ships_placed(game->players[i].fleet))
        {
            return 0;
        }
    }

    return 1;
}

int can_place_ship(const char board[BOARD_SIZE][BOARD_SIZE],
                   const struct Ship *ship,
                   int row,
                   int column,
                   int direction)
{
    if (ship == NULL ||
        ship->length <= 0 || ship->length > BOARD_SIZE ||
        (direction != HORIZONTAL && direction != VERTICAL) ||
        row < 0 || row >= BOARD_SIZE ||
        column < 0 || column >= BOARD_SIZE)
    {
        return 0;
    }

    for (int i = 0; i < ship->length; i++)
    {
        int check_row = row;
        int check_column = column;

        if (direction == HORIZONTAL)
        {
            check_column += i;
        }
        else
        {
            check_row += i;
        }

        if (check_row >= BOARD_SIZE || check_column >= BOARD_SIZE)
        {
            return 0;
        }

        if (board[check_row][check_column] != EMPTY_CELL)
        {
            return 0;
        }
    }

    return 1;
}

int place_ship(char board[BOARD_SIZE][BOARD_SIZE],
               struct Ship *ship,
               int row,
               int column,
               int direction)
{
    if (ship == NULL || ship->placed ||
        !can_place_ship(board, ship, row, column, direction))
    {
        return 0;
    }

    for (int i = 0; i < ship->length; i++)
    {
        int place_row = row;
        int place_column = column;

        if (direction == HORIZONTAL)
        {
            place_column += i;
        }
        else
        {
            place_row += i;
        }

        board[place_row][place_column] = SHIP_CELL;
    }

    ship->row = row;
    ship->column = column;
    ship->direction = direction;
    ship->placed = 1;

    return 1;
}

int shoot(char board[BOARD_SIZE][BOARD_SIZE], int row, int column)
{
    if (row < 0 || row >= BOARD_SIZE ||
        column < 0 || column >= BOARD_SIZE)
    {
        return SHOT_OUT_OF_BOUNDS;
    }

    if (board[row][column] == HIT_CELL ||
        board[row][column] == MISS_CELL)
    {
        return SHOT_ALREADY_TAKEN;
    }

    if (board[row][column] == SHIP_CELL)
    {
        board[row][column] = HIT_CELL;
        return SHOT_HIT;
    }

    board[row][column] = MISS_CELL;
    return SHOT_MISS;
}

int is_ship_sunk(const char board[BOARD_SIZE][BOARD_SIZE],
                 const struct Ship *ship)
{
    if (ship == NULL || !ship->placed)
    {
        return 0;
    }

    for (int i = 0; i < ship->length; i++)
    {
        int check_row = ship->row;
        int check_column = ship->column;

        if (ship->direction == HORIZONTAL)
        {
            check_column += i;
        }
        else
        {
            check_row += i;
        }

        if (board[check_row][check_column] != HIT_CELL)
        {
            return 0;
        }
    }

    return 1;
}

int all_ships_sunk(const char board[BOARD_SIZE][BOARD_SIZE],
                   const struct Ship fleet[FLEET_SIZE])
{
    for (int i = 0; i < FLEET_SIZE; i++)
    {
        if (!is_ship_sunk(board, &fleet[i]))
        {
            return 0;
        }
    }

    return 1;
}

int play_turn(struct Game *game, int row, int column)
{
    if (game == NULL ||
        game->current_turn < 0 || game->current_turn >= PLAYER_COUNT)
    {
        return PLAY_INVALID_GAME;
    }

    if (game->game_over)
    {
        return PLAY_GAME_OVER;
    }

    if (!game_is_ready(game))
    {
        return PLAY_NOT_READY;
    }

    int attacker = game->current_turn;
    int defender = 1 - attacker;
    int result = shoot(game->players[defender].board, row, column);

    if (result != SHOT_HIT && result != SHOT_MISS)
    {
        return result;
    }

    if (all_ships_sunk(game->players[defender].board,
                       game->players[defender].fleet))
    {
        game->game_over = 1;
        game->winner = attacker;
    }
    else if (result == SHOT_MISS)
    {
        game->current_turn = defender;
    }

    return result;
}
