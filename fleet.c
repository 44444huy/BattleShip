#include <string.h>
#include "server.h"

static int random_below(ServerRandom random, void *context, uint32_t bound, uint32_t *result)
{
    uint32_t value, threshold = (uint32_t)(-bound) % bound;
    do { if (!random(context, &value)) return 0; } while (value < threshold);
    *result = value % bound;
    return 1;
}
/* Each candidate is tried at most once at a depth. Backtracking restores the
 * complete player value, including fleet metadata, before trying another. */
static int fill(struct Player *player, int ship, ServerRandom random, void *context)
{
    if (ship == FLEET_SIZE) return 1;
    enum { CANDIDATES = BOARD_SIZE * BOARD_SIZE * 2 };
    int positions[CANDIDATES];
    for (int i = 0; i < CANDIDATES; ++i) positions[i] = i;
    for (int i = CANDIDATES - 1; i > 0; --i) {
        uint32_t j;
        if (!random_below(random, context, (uint32_t)i + 1, &j)) return -1;
        int temporary = positions[i]; positions[i] = positions[j]; positions[j] = temporary;
    }
    struct Player original = *player;
    for (int i = 0; i < CANDIDATES; ++i) {
        int cell = positions[i] / 2, direction = positions[i] % 2;
        if (!place_ship(player->board, &player->fleet[ship], cell / BOARD_SIZE, cell % BOARD_SIZE, direction)) continue;
        int result = fill(player, ship + 1, random, context);
        if (result != 0) return result;
        *player = original;
    }
    return 0;
}
int generate_random_fleet(struct Player *player, ServerRandom random, void *context)
{
    struct Player candidate;
    initialize_player(&candidate);
    if (fill(&candidate, 0, random, context) != 1) return 0;
    *player = candidate;
    return 1;
}
