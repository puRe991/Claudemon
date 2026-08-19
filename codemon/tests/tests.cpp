// ---------------------------------------------------------------------------
// Headless unit tests for the Monsta engine core logic.
//
// Deliberately exercises only the parts that do NOT need a graphics context /
// display: Coordinates, Tile and the hand-rolled Linked_list. The window,
// map-rendering and sprite loading paths require an OpenGL context and are
// covered by actually running the game.
// ---------------------------------------------------------------------------
#include "Coordinates.h"
#include "Tile.h"
#include "direction.h"
#include "data_structures.h"

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

static void test_linked_list() {
    std::printf("[linked_list]\n");
    Linked_list list;
    CHECK(list.get_head() == nullptr);
    CHECK(list.get_tail() == nullptr);

    int a = 11;
    int b = 22;

    CHECK(list.add_node(&a) == true);
    CHECK(list.get_head() != nullptr);
    CHECK(list.get_head() == list.get_tail());
    CHECK(list.get_head()->get_data() == &a);

    CHECK(list.add_node(&b) == true);
    // New nodes are inserted at the head.
    CHECK(list.get_head()->get_data() == &b);
    CHECK(list.get_head()->get_next()->get_data() == &a);
}

int main() {
    std::printf("Running Monsta engine core tests...\n");
    test_coordinates();
    test_tile();
    test_direction();
    test_linked_list();

    if (failures == 0) {
        std::printf("OK: all core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s) failed.\n", failures);
    return EXIT_FAILURE;
}
