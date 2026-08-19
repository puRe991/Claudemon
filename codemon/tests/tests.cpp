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
#include "Region.h"

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

int main() {
    std::printf("Running Monsta engine core tests...\n");
    test_coordinates();
    test_tile();
    test_tile_terrain();
    test_direction();
    test_linked_list();
    test_region();

    if (failures == 0) {
        std::printf("OK: all core tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s) failed.\n", failures);
    return EXIT_FAILURE;
}
