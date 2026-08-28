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
#include "TileMap.h"
#include "direction.h"
#include "Region.h"
#include "MenuModel.h"
#include "view_utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

// Path to the region manifest, injected by CMake so the test finds the data
// regardless of the working directory it is launched from.
#ifndef REGION_MANIFEST_PATH
#define REGION_MANIFEST_PATH "region/kanto.region"
#endif

static void test_tile_terrain() {
    std::printf("[tile terrain]\n");
    // Legacy ids must stay pinned - the old map sheet and saves rely on them.
    CHECK((int)Tile::short_grass == 0);
    CHECK((int)Tile::long_grass == 1);
    // The expanded vocabulary the region maps are authored against.
    CHECK((int)Tile::path == 2);
    CHECK((int)Tile::water == 3);
    CHECK((int)Tile::cave_wall == 12);
    CHECK((int)Tile::warp == 16);
    CHECK((int)Tile::deep_water == 20);
}

static void test_region() {
    std::printf("[region]\n");
    Region region(REGION_MANIFEST_PATH);
    CHECK(region.loaded());
    if (!region.loaded()) {
        std::printf("  (region manifest not found at %s - skipping)\n",
                    REGION_MANIFEST_PATH);
        return;
    }

    // The full described region: 30 areas wired by 66 (33 reciprocal) warps.
    CHECK(region.area_count() == 30);
    CHECK(region.warp_count() == 66);

    // The journey begins in the southern coastal village.
    const RegionArea* start = region.start_area();
    CHECK(start != nullptr);
    if (start != nullptr) {
        CHECK(start->id == "pallet_town");
        CHECK(start->start.get_x() == 7);
        CHECK(start->start.get_y() == 6);
    }

    // A named area resolves and carries its map file + display name.
    const RegionArea* indigo = region.get_area("indigo_plateau");
    CHECK(indigo != nullptr);
    if (indigo != nullptr) {
        CHECK(indigo->name == "Indigo Plateau");
        CHECK(indigo->map_path == "region/maps/indigo_plateau.map");
    }
    CHECK(region.get_area("no_such_place") == nullptr);

    // Stepping onto Pallet Town's north exit warps north to Route 1.
    const RegionWarp* north = region.warp_at("pallet_town", Coordinates(7, 0));
    CHECK(north != nullptr);
    if (north != nullptr) {
        CHECK(north->to_id == "route_01");
    }
    // A tile with no warp yields nothing.
    CHECK(region.warp_at("pallet_town", Coordinates(3, 3)) == nullptr);

    // Every warp is reciprocal: some warp leads back from its destination to
    // its origin. This is what keeps the region traversable in both ways.
    bool all_reciprocal = true;
    for (const RegionWarp& w : region.warps()) {
        bool has_return = false;
        for (const RegionWarp& r : region.warps()) {
            if (r.from_id == w.to_id && r.to_id == w.from_id) {
                has_return = true;
                break;
            }
        }
        if (!has_return) {
            all_reciprocal = false;
            std::printf("  no return warp for %s -> %s\n",
                        w.from_id.c_str(), w.to_id.c_str());
        }
    }
    CHECK(all_reciprocal);

    // Connectivity: every area is reachable from the start by walking warps.
    // A hand-rolled BFS over area ids - no <set>/<map> to keep it lightweight.
    std::vector<std::string> seen;
    seen.push_back(start->id);
    for (std::size_t i = 0; i < seen.size(); ++i) {
        std::vector<std::string> next = region.neighbors(seen[i]);
        for (const std::string& n : next) {
            bool known = false;
            for (const std::string& s : seen) {
                if (s == n) { known = true; break; }
            }
            if (!known) {
                seen.push_back(n);
            }
        }
    }
    CHECK(seen.size() == region.area_count());

    // Every warp points at a real, declared area (no dangling ids).
    bool all_targets_exist = true;
    for (const RegionWarp& w : region.warps()) {
        if (region.get_area(w.from_id) == nullptr ||
            region.get_area(w.to_id) == nullptr) {
            all_targets_exist = false;
        }
    }
    CHECK(all_targets_exist);
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
    test_tile_terrain();
    test_direction();
    test_region();
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
