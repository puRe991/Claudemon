#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Region.h"

#include <list>
#include <string>

//Convert the key input into direction input
DIR convert_key_event(sf::Event *event) {
    DIR input_dir = DIR::NONE;

    //Recode event as a input direction for game state.
    //Up was pressed
    if (event->key.code == sf::Keyboard::W) {
        input_dir = DIR::N;
    }
    //Down was pressed
    else if (event->key.code == sf::Keyboard::S) {
        input_dir = DIR::S;
    }
    //Left was pressed
    else if (event->key.code == sf::Keyboard::A) {
        input_dir = DIR::W;
    }
    //Right was pressed
    else if (event->key.code == sf::Keyboard::D) {
        input_dir = DIR::E;
    }
    return input_dir;
}

// Tiles a character cannot walk onto. Everything else (grass, path, sand,
// floors, cave floors, bridges, stairs, warps, ...) is walkable.
static bool is_walkable(Tile::tile t) {
    switch (t) {
    case Tile::water:
    case Tile::tree:
    case Tile::rock:
    case Tile::building:
    case Tile::wall:
    case Tile::cave_wall:
    case Tile::sign:
    case Tile::lava:
    case Tile::deep_water:
        return false;
    default:
        return true;
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

//Welcome to Codemon!
int main()
{
    // Main screen: a VIEW_TILES_X x VIEW_TILES_Y window into the world, drawn
    // at SCALE zoom. The camera (set each frame) scrolls the map beneath it.
    Window scr(VIEW_TILES_X * TILE_PX * SCALE,
               VIEW_TILES_Y * TILE_PX * SCALE, "Codemon!");

    //Just keep track of all the windows created
    std::list<Window> windows_list;
    windows_list.push_front(scr);

    //Create Test player character
    Character player;
    player.load_sprite_sheet();

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

    // Keep the drawn sprite on the player's tile (position is in pixels).
    player.set_x(player_tile.get_x() * 32);
    player.set_y(player_tile.get_y() * 32);

    //Flag declarations
    bool key_input = false;
    DIR input_dir = NONE;

    //So long as the active_window is still open.
    while (scr.get_window()->isOpen())
    {
        //Create a storage event for processing system/input events
        sf::Event event;
        while (scr.get_event(&event))
        {
            //If the player has closed the game
            switch (event.type) {
            //Close button clicked
            case sf::Event::Closed:
                scr.close();
                break;
            //A keyboard key has been pressed
            case sf::Event::KeyPressed:
                key_input = true;
                input_dir = convert_key_event(&event);
                break;
            }
        }
        /*
        Process Game State + Input
        */

        //If a keyboard input was pressed
        if (key_input && input_dir != DIR::NONE) {
            // Work out the target tile for this move.
            Coordinates target = player_tile;
            switch (input_dir) {
            case DIR::N: if (player_tile.get_y() > 0) target.set_y(player_tile.get_y() - 1); break;
            case DIR::S: target.set_y(player_tile.get_y() + 1); break;
            case DIR::W: if (player_tile.get_x() > 0) target.set_x(player_tile.get_x() - 1); break;
            case DIR::E: target.set_x(player_tile.get_x() + 1); break;
            default: break;
            }

            // Only step there if it is on the map and not blocked terrain.
            if (game_map->in_bounds(target) && is_walkable(game_map->tile_at(target))) {
                player_tile = target;

                // Did we just step onto a warp? If so, switch to the target
                // area's map and drop the player at the warp's destination.
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
                    }
                }
            }

            // Face the input direction and advance the walk animation.
            player.set_facing(input_dir);
            player.set_x(player_tile.get_x() * 32);
            player.set_y(player_tile.get_y() * 32);
            player.update_sprite_pos();

            //Reset the flags
            key_input = false;
            input_dir = DIR::NONE;
        }

        /*
        Render loop portion
        */

        // Scroll the camera to follow the player, clamped to the map edges.
        const float view_w = VIEW_TILES_X * TILE_PX;
        const float view_h = VIEW_TILES_Y * TILE_PX;
        float cam_x = clamp_camera(player.get_x() + TILE_PX / 2.0f,
                                   view_w / 2.0f,
                                   game_map->get_width() * (float)TILE_PX);
        float cam_y = clamp_camera(player.get_y() + TILE_PX / 2.0f,
                                   view_h / 2.0f,
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
