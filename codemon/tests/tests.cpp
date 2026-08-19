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
#include "TileMap.h"
#include "direction.h"
#include "data_structures.h"
#include "MenuModel.h"
#include "view_utils.h"

#include <cmath>
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

static void test_tilemap() {
    std::printf("[tilemap]\n");

    TileMap m(4, 3, 0);
    CHECK(m.get_width() == 4);
    CHECK(m.get_height() == 3);
    CHECK(m.in_bounds(3, 2));
    CHECK(!m.in_bounds(4, 2));
    CHECK(m.get_tile(0, 0) == 0);
    CHECK(m.get_tile(99, 99) == -1); // out of range read

    m.set_tile(1, 2, 7);
    CHECK(m.get_tile(1, 2) == 7);
    m.set_tile(99, 99, 5); // out of range write is ignored
    CHECK(m.get_tile(99, 99) == -1);

    m.set_start(2, 1);
    CHECK(m.get_start_x() == 2);
    CHECK(m.get_start_y() == 1);

    // resize preserves overlapping tiles and clamps the spawn
    m.resize(2, 2, 0);
    CHECK(m.get_width() == 2);
    CHECK(m.get_start_x() == 1); // clamped from x=2 into a width-2 map

    // save -> load round trip
    const char* path = "tilemap_roundtrip.tmp";
    TileMap src(3, 2, 0);
    src.set_tile(0, 0, 1);
    src.set_tile(2, 1, 9);
    src.set_start(1, 1);
    CHECK(src.save_to_file(path));

    TileMap dst;
    CHECK(dst.load_from_file(path));
    CHECK(dst.get_width() == 3);
    CHECK(dst.get_height() == 2);
    CHECK(dst.get_tile(0, 0) == 1);
    CHECK(dst.get_tile(2, 1) == 9);
    CHECK(dst.get_tile(1, 0) == 0);
    CHECK(dst.get_start_x() == 1);
    CHECK(dst.get_start_y() == 1);
    std::remove(path);

    // loading a non-existent file fails cleanly
    TileMap missing;
    CHECK(!missing.load_from_file("does_not_exist_12345.tmp"));
}

static void test_menu_model() {
    std::printf("[menu_model]\n");

    MenuModel m;
    m.add_item("NEW GAME", MenuAction::NewGame,  true);
    m.add_item("CONTINUE", MenuAction::Continue, false); // disabled
    m.add_item("OPTIONS",  MenuAction::Options,  true);
    m.add_item("QUIT",     MenuAction::Quit,     true);

    // Starts on the first enabled item.
    CHECK(m.selected() == 0);
    CHECK(m.confirm() == MenuAction::NewGame);

    // Moving down skips the disabled "CONTINUE" (index 1) -> "OPTIONS" (index 2).
    m.move_down();
    CHECK(m.selected() == 2);
    CHECK(m.confirm() == MenuAction::Options);

    m.move_down();
    CHECK(m.selected() == 3); // QUIT

    // Wraps around back to the first enabled item.
    m.move_down();
    CHECK(m.selected() == 0);

    // Moving up from the top wraps to the last item, skipping disabled.
    m.move_up();
    CHECK(m.selected() == 3);
    m.move_up();
    CHECK(m.selected() == 2);
    m.move_up();
    CHECK(m.selected() == 0); // skipped disabled index 1
}

static void test_letterbox() {
    std::printf("[letterbox]\n");

    // Square content in a square window: fills everything.
    ViewportRect a = letterbox_viewport(512, 512, 512.f, 512.f);
    CHECK(std::fabs(a.x - 0.f) < 1e-4f);
    CHECK(std::fabs(a.w - 1.f) < 1e-4f);
    CHECK(std::fabs(a.h - 1.f) < 1e-4f);

    // Wider-than-tall window: pillarbox (bars left/right), full height.
    ViewportRect b = letterbox_viewport(1024, 512, 512.f, 512.f);
    CHECK(std::fabs(b.w - 0.5f) < 1e-4f);
    CHECK(std::fabs(b.x - 0.25f) < 1e-4f);
    CHECK(std::fabs(b.h - 1.f) < 1e-4f);

    // Taller-than-wide window: letterbox (bars top/bottom), full width.
    ViewportRect c = letterbox_viewport(512, 1024, 512.f, 512.f);
    CHECK(std::fabs(c.h - 0.5f) < 1e-4f);
    CHECK(std::fabs(c.y - 0.25f) < 1e-4f);
    CHECK(std::fabs(c.w - 1.f) < 1e-4f);

    // Degenerate inputs fall back to the full viewport.
    ViewportRect d = letterbox_viewport(0, 0, 512.f, 512.f);
    CHECK(std::fabs(d.w - 1.f) < 1e-4f);
    CHECK(std::fabs(d.h - 1.f) < 1e-4f);
}

int main() {
    std::printf("Running Monsta engine core tests...\n");
    test_coordinates();
    test_tile();
    test_direction();
    test_linked_list();
    test_tilemap();
    test_menu_model();
    test_letterbox();

    if (failures == 0) {
        std::printf("OK: all core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s) failed.\n", failures);
    return EXIT_FAILURE;
}
