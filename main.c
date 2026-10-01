#include <stdio.h>
#include "game.h"

int setup_player(struct Player *player, int player_number)
{
    char input[100];

    printf("\nPlayer %d, place your fleet.\n", player_number);

    for (int i = 0; i < FLEET_SIZE; i++)
    {
        while (!player->fleet[i].placed)
        {
            int row;
            int column;
            int direction;

            print_board(player->board);
            printf("\nPlace %s (length %d).\n",
                   player->fleet[i].name, player->fleet[i].length);
            printf("Starting coordinate (A1-J10): ");

            if (fgets(input, sizeof(input), stdin) == NULL)
            {
                return 0;
            }

            if (!parse_coordinate(input, &row, &column))
            {
                printf("Invalid coordinate. Try again.\n\n");
                continue;
            }

            printf("Direction (H for horizontal, V for vertical): ");

            if (fgets(input, sizeof(input), stdin) == NULL)
            {
                return 0;
            }

            if (!parse_direction(input, &direction))
            {
                printf("Invalid direction. Try again.\n\n");
                continue;
            }

            if (!place_ship(player->board, &player->fleet[i],
                            row, column, direction))
            {
                printf("The ship does not fit or overlaps another ship. "
                       "Try again.\n\n");
            }
        }
    }

    printf("\nPlayer %d's fleet is ready.\n", player_number);
    print_board(player->board);
    return 1;
}

void print_player_boards(const struct Game *game, int player_index)
{
    char opponent_view[BOARD_SIZE][BOARD_SIZE];
    int opponent_index = 1 - player_index;

    printf("\nPlayer %d - My Board:\n", player_index + 1);
    print_board(game->players[player_index].board);

    create_opponent_view(game->players[opponent_index].board, opponent_view);
    printf("\nPlayer %d - Opponent Board:\n", player_index + 1);
    print_board(opponent_view);
}

int main(void)
{
    struct Game game;
    char input[100];
    initialize_game(&game);

    if (!setup_player(&game.players[0], 1) ||
        !setup_player(&game.players[1], 2))
    {
        printf("\nInput ended during fleet setup.\n");
        return 0;
    }

    printf("Both players are ready.\n");

    while (!game.game_over)
    {
        int player = game.current_turn;
        int row;
        int column;

        print_player_boards(&game, player);
        printf("\nPlayer %d, enter a coordinate (A1-J10): ", player + 1);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("\nInput ended.\n");
            break;
        }

        if (!parse_coordinate(input, &row, &column))
        {
            printf("Invalid coordinate. Try again.\n");
            continue;
        }

        int result = play_turn(&game, row, column);

        if (result == SHOT_HIT)
        {
            printf("HIT!\n");
        }
        else if (result == SHOT_MISS)
        {
            printf("MISS!\n");
        }
        else if (result == SHOT_ALREADY_TAKEN)
        {
            printf("That cell was already fired at. Try again.\n");
        }
    }

    if (game.game_over)
    {
        printf("\nPlayer %d wins!\n", game.winner + 1);
    }

    return 0;
}
