#include <stdio.h>
#include "game.h"

int failed_tests = 0;

void print_test_result(const char *test_name, int passed)
{
    if (passed)
    {
        printf("PASS: %s\n", test_name);
    }
    else
    {
        printf("FAIL: %s\n", test_name);
        failed_tests++;
    }
}

int place_test_fleet(struct Player *player)
{
    return place_ship(player->board, &player->fleet[0], 0, 0, HORIZONTAL) &&
           place_ship(player->board, &player->fleet[1], 2, 0, VERTICAL) &&
           place_ship(player->board, &player->fleet[2], 2, 2, HORIZONTAL) &&
           place_ship(player->board, &player->fleet[3], 4, 2, HORIZONTAL) &&
           place_ship(player->board, &player->fleet[4], 7, 7, VERTICAL);
}

int board_contains_cell(const char board[BOARD_SIZE][BOARD_SIZE], char cell)
{
    for (int row = 0; row < BOARD_SIZE; row++)
    {
        for (int column = 0; column < BOARD_SIZE; column++)
        {
            if (board[row][column] == cell)
            {
                return 1;
            }
        }
    }

    return 0;
}

int main(void)
{
    int parsed_row;
    int parsed_column;

    print_test_result(
        "Parse coordinate A1",
        parse_coordinate("A1\n", &parsed_row, &parsed_column) == 1 &&
        parsed_row == 0 && parsed_column == 0
    );

    print_test_result(
        "Parse lowercase coordinate j10",
        parse_coordinate("j10", &parsed_row, &parsed_column) == 1 &&
        parsed_row == 9 && parsed_column == 9
    );

    print_test_result(
        "Reject invalid row letter",
        parse_coordinate("K1", &parsed_row, &parsed_column) == 0
    );

    print_test_result(
        "Reject invalid column number",
        parse_coordinate("A0", &parsed_row, &parsed_column) == 0 &&
        parse_coordinate("A11", &parsed_row, &parsed_column) == 0
    );

    print_test_result(
        "Reject extra coordinate characters",
        parse_coordinate("A1abc", &parsed_row, &parsed_column) == 0
    );

    int parsed_direction;

    print_test_result(
        "Parse horizontal direction",
        parse_direction("h\n", &parsed_direction) == 1 &&
        parsed_direction == HORIZONTAL
    );

    print_test_result(
        "Parse vertical direction",
        parse_direction("V", &parsed_direction) == 1 &&
        parsed_direction == VERTICAL
    );

    print_test_result(
        "Reject invalid direction text",
        parse_direction("horizontal", &parsed_direction) == 0 &&
        parse_direction("X", &parsed_direction) == 0
    );

    char board[BOARD_SIZE][BOARD_SIZE];
    struct Ship carrier = {"Carrier", 5, -1, -1, HORIZONTAL, 0};
    struct Ship battleship = {"Battleship", 4, -1, -1, HORIZONTAL, 0};
    struct Ship cruiser = {"Cruiser", 3, -1, -1, HORIZONTAL, 0};
    struct Ship invalid_ship = {"Invalid", 0, -1, -1, HORIZONTAL, 0};

    initialize_board(board);

    print_test_result(
        "Valid horizontal placement",
        place_ship(board, &carrier, 2, 1, HORIZONTAL) == 1
    );

    print_test_result(
        "Reject placement beyond right edge",
        place_ship(board, &battleship, 0, 8, HORIZONTAL) == 0
    );

    print_test_result(
        "Reject placement beyond bottom edge",
        place_ship(board, &battleship, 8, 0, VERTICAL) == 0
    );

    print_test_result(
        "Reject overlapping ships",
        place_ship(board, &cruiser, 2, 3, HORIZONTAL) == 0
    );

    print_test_result(
        "Reject placing the same ship twice",
        place_ship(board, &carrier, 5, 0, VERTICAL) == 0
    );

    print_test_result(
        "Reject invalid direction",
        can_place_ship(board, &cruiser, 5, 0, 99) == 0
    );

    print_test_result(
        "Reject invalid ship length",
        can_place_ship(board, &invalid_ship, 5, 0, HORIZONTAL) == 0
    );

    char fleet_board[BOARD_SIZE][BOARD_SIZE];
    struct Ship fleet[FLEET_SIZE];

    initialize_board(fleet_board);
    initialize_fleet(fleet);

    print_test_result(
        "Fleet starts with five unplaced ships",
        fleet[0].length == 5 &&
        fleet[1].length == 4 &&
        fleet[2].length == 3 &&
        fleet[3].length == 3 &&
        fleet[4].length == 2 &&
        all_ships_placed(fleet) == 0
    );

    place_ship(fleet_board, &fleet[0], 0, 0, HORIZONTAL);
    place_ship(fleet_board, &fleet[1], 2, 0, VERTICAL);
    place_ship(fleet_board, &fleet[2], 2, 2, HORIZONTAL);
    place_ship(fleet_board, &fleet[3], 4, 2, HORIZONTAL);
    place_ship(fleet_board, &fleet[4], 7, 7, VERTICAL);

    print_test_result(
        "All five fleet placements are valid",
        fleet[0].placed && fleet[1].placed && fleet[2].placed &&
        fleet[3].placed && fleet[4].placed
    );

    print_test_result(
        "Fleet is ready after all ships are placed",
        all_ships_placed(fleet) == 1
    );

    print_test_result(
        "Shot hits a ship",
        shoot(fleet_board, 0, 0) == SHOT_HIT &&
        fleet_board[0][0] == HIT_CELL
    );

    print_test_result(
        "Shot misses on an empty cell",
        shoot(fleet_board, 9, 9) == SHOT_MISS &&
        fleet_board[9][9] == MISS_CELL
    );

    print_test_result(
        "Reject shooting the same cell twice",
        shoot(fleet_board, 0, 0) == SHOT_ALREADY_TAKEN &&
        shoot(fleet_board, 9, 9) == SHOT_ALREADY_TAKEN
    );

    print_test_result(
        "Reject shot outside the board",
        shoot(fleet_board, -1, 0) == SHOT_OUT_OF_BOUNDS &&
        shoot(fleet_board, 0, BOARD_SIZE) == SHOT_OUT_OF_BOUNDS
    );

    shoot(fleet_board, 7, 7);

    print_test_result(
        "Ship is not sunk after only one hit",
        is_ship_sunk(fleet_board, &fleet[4]) == 0
    );

    shoot(fleet_board, 8, 7);

    print_test_result(
        "Ship is sunk after all its cells are hit",
        is_ship_sunk(fleet_board, &fleet[4]) == 1
    );

    print_test_result(
        "Game continues while other ships remain",
        all_ships_sunk(fleet_board, fleet) == 0
    );

    for (int ship_index = 0; ship_index < FLEET_SIZE; ship_index++)
    {
        for (int cell = 0; cell < fleet[ship_index].length; cell++)
        {
            int row = fleet[ship_index].row;
            int column = fleet[ship_index].column;

            if (fleet[ship_index].direction == HORIZONTAL)
            {
                column += cell;
            }
            else
            {
                row += cell;
            }

            shoot(fleet_board, row, column);
        }
    }

    print_test_result(
        "Game ends after all ships are sunk",
        all_ships_sunk(fleet_board, fleet) == 1
    );

    struct Game game;
    initialize_game(&game);

    print_test_result(
        "Game starts with Player 1 and no winner",
        game.current_turn == 0 && game.game_over == 0 &&
        game.winner == NO_WINNER
    );

    print_test_result(
        "Reject turn before both fleets are placed",
        play_turn(&game, 0, 0) == PLAY_NOT_READY &&
        game.current_turn == 0
    );

    print_test_result(
        "Both players can prepare their fleets",
        place_test_fleet(&game.players[0]) &&
        place_test_fleet(&game.players[1]) &&
        game_is_ready(&game)
    );

    char opponent_view[BOARD_SIZE][BOARD_SIZE];
    create_opponent_view(game.players[1].board, opponent_view);

    print_test_result(
        "Opponent view hides every unhit ship",
        !board_contains_cell(opponent_view, SHIP_CELL) &&
        opponent_view[0][0] == EMPTY_CELL
    );

    print_test_result(
        "Valid hit keeps Player 1's turn",
        play_turn(&game, 0, 0) == SHOT_HIT &&
        game.current_turn == 0
    );

    create_opponent_view(game.players[1].board, opponent_view);

    print_test_result(
        "Opponent view shows a hit",
        opponent_view[0][0] == HIT_CELL
    );

    print_test_result(
        "Valid miss changes to Player 2's turn",
        play_turn(&game, 9, 9) == SHOT_MISS &&
        game.current_turn == 1
    );

    create_opponent_view(game.players[1].board, opponent_view);

    print_test_result(
        "Opponent view shows a miss",
        opponent_view[9][9] == MISS_CELL &&
        !board_contains_cell(opponent_view, SHIP_CELL)
    );

    print_test_result(
        "Player 2 keeps the turn after a hit",
        play_turn(&game, 0, 0) == SHOT_HIT &&
        game.current_turn == 1
    );

    print_test_result(
        "Repeated shot does not change the turn",
        play_turn(&game, 0, 0) == SHOT_ALREADY_TAKEN &&
        game.current_turn == 1
    );

    print_test_result(
        "Out-of-bounds shot does not change the turn",
        play_turn(&game, -1, 0) == SHOT_OUT_OF_BOUNDS &&
        game.current_turn == 1
    );

    for (int ship_index = 0; ship_index < FLEET_SIZE; ship_index++)
    {
        for (int cell = 0; cell < game.players[1].fleet[ship_index].length; cell++)
        {
            int row = game.players[1].fleet[ship_index].row;
            int column = game.players[1].fleet[ship_index].column;

            if (game.players[1].fleet[ship_index].direction == HORIZONTAL)
            {
                column += cell;
            }
            else
            {
                row += cell;
            }

            game.current_turn = 0;
            play_turn(&game, row, column);
        }
    }

    print_test_result(
        "Player 1 wins after sinking Player 2's fleet",
        game.game_over == 1 && game.winner == 0
    );

    print_test_result(
        "Reject turns after the game is over",
        play_turn(&game, 1, 1) == PLAY_GAME_OVER
    );

    return failed_tests == 0 ? 0 : 1;
}
