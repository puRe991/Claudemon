#pragma once
#include <string>
#include <vector>

#include "Tileset.h"
#include "Window.h"
#include "SFML/Graphics.hpp"
#include "direction.h"
#include "Coordinates.h"

/******************************************************************************
Map - a grid of metatile ids rendered through a Tileset.

Map file format (text), e.g. maps/route.map:

    tileset general        # metatile sheet: assets/tilesets/general.png
    20 15                  # width height (in metatiles)
    3 6                    # player start tile (x y)
    <height rows of width comma-separated metatile ids>
    collision              # optional marker
    <height rows of width comma-separated 0/1 passability flags>

Rendering samples 16x16 metatiles from the coloured tileset sheet, so the map
looks exactly like the source pokeemerald tiles with no runtime palette work.
*****************************************************************************/
class Map
{
private:
	Tileset tileset;             // coloured metatile sheet
	int tile_px;                 // metatile edge length (px), from the tileset
	Coordinates dimensions;      // width, height in tiles
	Coordinates start_pos;       // player spawn tile

	std::vector<int>  tile_map;  // width*height metatile ids
	std::vector<char> solid;     // width*height passability (1 = blocked)

	int index(int x, int y) const;

public:
	// Load a map file; tileset_dir is where the coloured sheets live.
	Map(const std::string& map_path,
	    const std::string& tileset_dir = "assets/tilesets");

	// Render every metatile into the window.
	void render_map(Window* active_window);
	// Render every metatile into any SFML target (window or off-screen texture).
	void render_to(sf::RenderTarget& target);

	// Is (tile_x, tile_y) inside the map?
	bool in_bounds(int tile_x, int tile_y) const;
	bool in_bounds(Coordinates proposed_coord) const;

	// Can a character stand on (tile_x, tile_y)? (in bounds AND not blocked)
	bool passable(int tile_x, int tile_y) const;

	unsigned int get_width() const;
	unsigned int get_height() const;
	int get_tile_size() const;
	Coordinates get_start_pos() const;
	bool ready() const;
};
