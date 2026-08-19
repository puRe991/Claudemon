#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Region.h"
#include "Audio.h"

#include <list>
#include <string>

// Which movement key is currently held (real-time state, so holding a key
// keeps you walking). North wins ties, then S, W, E.
static DIR held_direction() {
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) return DIR::N;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) return DIR::S;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) return DIR::W;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) return DIR::E;
    return DIR::NONE;
}

// Ground the player can stand on. Everything else - water, trees, buildings,
// roofs, walls, fences and every multi-tile building/decor stamp (ids >= 25) -
// blocks. Listing the walkable ids (rather than the blocking ones) keeps new
// decorative tiles solid by default.
static bool is_walkable(Tile::tile t) {
    switch (t) {
    case Tile::short_grass:
    case Tile::long_grass:
    case Tile::path:
    case Tile::sand:
    case Tile::flower:
    case Tile::floor:
    case Tile::cave_floor:
    case Tile::ledge:
    case Tile::bridge:
    case Tile::stairs:
    case Tile::warp:
    case Tile::ice:
        return true;
    default:
        return false;
    }
}

// Shared terrain tile sheet for the region: one row of tiles indexed by
// Tile::tile id (see region/region_tiles.png). Stands in for per-area art.
static const std::string REGION_SHEET = "region/region_tiles.png";

// Camera viewport, in tiles, and an on-screen zoom. The window shows
// VIEW_TILES_X x VIEW_TILES_Y tiles of the world at a time and the camera
// scrolls to keep the player centred; SCALE just makes each tile chunkier on
// screen. These are smaller than the maps, so the world scrolls underneath.
static const int VIEW_TILES_X = 11;
static const int VIEW_TILES_Y = 9;
static const int SCALE = 2;
static const int TILE_PX = 32;

// Walk speed in pixels per frame (at 60 fps). Must divide TILE_PX so a step
// lands exactly on the next tile: 4 px -> 8 frames per tile (~130 ms).
static const float MOVE_PX = 4.0f;

// Keep a camera axis inside the map: centre on the target, but never scroll
// past an edge. If the map is smaller than the view, centre the whole map.
static float clamp_camera(float center, float half_view, float map_size) {
    if (map_size <= half_view * 2.0f) {
        return map_size / 2.0f;
    }
    if (center < half_view) {
        return half_view;
    }
    if (center > map_size - half_view) {
        return map_size - half_view;
    }
    return center;
}

// The tile a step in `dir` from `from` would land on. Returns false (and leaves
// `out` unchanged) when the step would run off the top/left edge.
static bool step_target(Coordinates from, DIR dir, Coordinates* out) {
    unsigned int x = from.get_x(), y = from.get_y();
    switch (dir) {
    case DIR::N: if (y == 0) return false; *out = Coordinates(x, y - 1); return true;
    case DIR::S: *out = Coordinates(x, y + 1); return true;
    case DIR::W: if (x == 0) return false; *out = Coordinates(x - 1, y); return true;
    case DIR::E: *out = Coordinates(x + 1, y); return true;
    default: return false;
    }
}

//Welcome to Codemon!
int main()
{
    // Main screen: a VIEW_TILES_X x VIEW_TILES_Y window into the world, drawn
    // at SCALE zoom. The camera (set each frame) scrolls the map beneath it.
    Window scr(VIEW_TILES_X * TILE_PX * SCALE,
               VIEW_TILES_Y * TILE_PX * SCALE, "Codemon!");
    // Cap the framerate so walk speed (pixels per frame) is machine-independent.
    scr.get_window()->setFramerateLimit(60);

    //Just keep track of all the windows created
    std::list<Window> windows_list;
    windows_list.push_front(scr);

    //Create Test player character
    Character player;
    player.load_sprite_sheet();

    // Sound effects. Missing files are silently ignored, so the game still
    // runs without audio; drop your own licensed audio into assets/sfx/.
    Audio audio;
    audio.load("bump", "assets/sfx/bump.wav");
    audio.load("warp", "assets/sfx/warp.wav");

    // Load the region structure. The manifest wires ~30 areas together with
    // warps; the player roams one area map at a time and warps between them.
    Region region("region/kanto.region");

    // Current area + its loaded map. Fall back to the legacy demo map if the
    // region manifest is missing so the game still starts.
    std::string current_area;
    Coordinates player_tile(0, 0);
    Map* game_map = nullptr;

    if (region.loaded() && region.start_area() != nullptr) {
        const RegionArea* start = region.start_area();
        current_area = start->id;
        player_tile = start->start;
        game_map = new Map(start->map_path, REGION_SHEET);
    } else {
        game_map = new Map("maps/map_00.txt", "maps/map_set/map_00.png");
        player_tile = game_map->get_start_pos();
    }

    // Smooth-movement state. (px,py) is the player's on-screen pixel position;
    // while moving it glides toward (tx,ty), the target tile's pixel position.
    float px = player_tile.get_x() * (float)TILE_PX;
    float py = player_tile.get_y() * (float)TILE_PX;
    float tx = px, ty = py;
    bool moving = false;
    int anim_tick = 0;
    DIR blocked_dir = DIR::NONE;   // last direction we bumped, to debounce the sfx

    player.set_x((int)px);
    player.set_y((int)py);
    player.set_standing();

    //So long as the active_window is still open.
    while (scr.get_window()->isOpen())
    {
        //Drain system events. Movement is read from live key state below, so
        //here we only care about the window being closed.
        sf::Event event;
        while (scr.get_event(&event))
        {
            if (event.type == sf::Event::Closed) {
                scr.close();
            }
        }

        /*
        Process Game State + Input
        */

        // Only accept a new step when the previous one has finished, so the
        // grid stays aligned and movement animates one tile at a time.
        if (!moving) {
            DIR d = held_direction();
            if (d != DIR::NONE) {
                player.set_facing(d);
                Coordinates target(0, 0);
                if (step_target(player_tile, d, &target) &&
                    game_map->in_bounds(target) &&
                    is_walkable(game_map->tile_at(target))) {
                    // Begin gliding toward the target tile.
                    player_tile = target;
                    tx = target.get_x() * (float)TILE_PX;
                    ty = target.get_y() * (float)TILE_PX;
                    moving = true;
                    blocked_dir = DIR::NONE;
                } else {
                    // Blocked or at an edge: just turn to face that way, and
                    // thud once (not every frame the key is held into a wall).
                    player.set_standing();
                    if (d != blocked_dir) {
                        audio.play("bump");
                        blocked_dir = d;
                    }
                }
            } else {
                player.set_standing();
                blocked_dir = DIR::NONE;
            }
        }

        if (moving) {
            // Advance the glide toward the target on the moving axis.
            if (px < tx) px = (px + MOVE_PX > tx) ? tx : px + MOVE_PX;
            else if (px > tx) px = (px - MOVE_PX < tx) ? tx : px - MOVE_PX;
            if (py < ty) py = (py + MOVE_PX > ty) ? ty : py + MOVE_PX;
            else if (py > ty) py = (py - MOVE_PX < ty) ? ty : py - MOVE_PX;

            player.set_x((int)px);
            player.set_y((int)py);
            // Shuffle the walk cycle a few times across the step.
            if (++anim_tick % 4 == 0) player.update_sprite_pos();
            else player.refresh_position();

            // Arrived on the tile centre?
            if (px == tx && py == ty) {
                moving = false;
                player.set_standing();

                // Stepping onto a warp tile switches areas on arrival.
                const RegionWarp* w = region.loaded()
                    ? region.warp_at(current_area, player_tile)
                    : nullptr;
                if (w != nullptr) {
                    const RegionArea* dest = region.get_area(w->to_id);
                    if (dest != nullptr) {
                        Map* next = new Map(dest->map_path, REGION_SHEET);
                        delete game_map;
                        game_map = next;
                        current_area = dest->id;
                        player_tile = w->to_pos;
                        px = tx = player_tile.get_x() * (float)TILE_PX;
                        py = ty = player_tile.get_y() * (float)TILE_PX;
                        player.set_x((int)px);
                        player.set_y((int)py);
                        player.set_standing();
                        audio.play("warp");
                    }
                }
            }
        }

        /*
        Render loop portion
        */

        // Scroll the camera to follow the player, clamped to the map edges.
        const float view_w = VIEW_TILES_X * TILE_PX;
        const float view_h = VIEW_TILES_Y * TILE_PX;
        float cam_x = clamp_camera(px + TILE_PX / 2.0f, view_w / 2.0f,
                                   game_map->get_width() * (float)TILE_PX);
        float cam_y = clamp_camera(py + TILE_PX / 2.0f, view_h / 2.0f,
                                   game_map->get_height() * (float)TILE_PX);
        scr.set_camera(cam_x, cam_y, view_w, view_h);

        scr.clear();
        //draw the sprite to the window
        game_map->render_map(&scr);
        scr.draw(&player);

        //Update the window to draw stuff
        scr.display();
    }

    delete game_map;
    return 0;
}
