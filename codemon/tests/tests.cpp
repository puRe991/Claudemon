// ---------------------------------------------------------------------------
// Headless unit tests for the Monsta engine core logic.
//
// Covers the small value types the game is built out of -- Coordinates, Tile
// and the DIR enum -- none of which need a graphics context.
//
// The game logic proper (BattleData, SaveGame, ScriptVM, Battle) lives in
// engine_tests.cpp; this file deliberately stays tiny.
// ---------------------------------------------------------------------------
#include "Coordinates.h"
#include "Tile.h"
#include "direction.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);       \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void test_coordinates() {
    std::printf("[coordinates]\n");
    Coordinates origin;
    CHECK(origin.get_x() == 0);
    CHECK(origin.get_y() == 0);

    Coordinates p(3, 4);
    CHECK(p.get_x() == 3);
    CHECK(p.get_y() == 4);

    p.set_x(7);
    p.set_y(9);
    CHECK(p.get_x() == 7);
    CHECK(p.get_y() == 9);

    Coordinates sum = Coordinates(1, 2) + Coordinates(3, 4);
    CHECK(sum.get_x() == 4);
    CHECK(sum.get_y() == 6);
}

static void test_tile() {
    std::printf("[tile]\n");
    Tile t(2, 5, Tile::long_grass);
    CHECK(t.get_x() == 2);
    CHECK(t.get_y() == 5);
    CHECK(t.get_data() == Tile::long_grass);

    Tile empty;
    CHECK(empty.get_x() == 0);
    CHECK(empty.get_y() == 0);
    CHECK(empty.get_data() == Tile::short_grass);
}

static void test_direction() {
    std::printf("[direction]\n");
    // All four movement directions must be distinct and separate from NONE.
    CHECK(DIR::N != DIR::S);
    CHECK(DIR::E != DIR::W);
    CHECK(DIR::N != DIR::NONE);
    CHECK(DIR::E != DIR::NONE);
}

int main() {
    std::printf("Running Monsta engine core tests...\n");
    test_coordinates();
    test_tile();
    test_direction();

    if (failures == 0) {
        std::printf("OK: all core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s) failed.\n", failures);
    return EXIT_FAILURE;
}
