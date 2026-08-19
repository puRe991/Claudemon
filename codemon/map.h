#pragma once
#include <string>

#include "SFML/Graphics.hpp"
#include "Window.h"
#include "Coordinates.h"
#include "TileMap.h"

/******************************************************************************
Map - a drawable tile map.

The tile data itself lives in a headless TileMap (robust CSV loader, supports
arbitrary sizes and a player spawn); this class adds the SFML tilesheet and
knows how to render the grid into a Window. Rendering samples the tilesheet
horizontally: a tile's integer id is its column in the sheet.
******************************************************************************/
class Map
{
private:
	TileMap m_data;             //grid data (ids, dimensions, spawn)
	sf::Texture map_sheet;      //tilesheet the ids index into
	unsigned int tile_w;        //tile size in pixels
	unsigned int tile_h;

	std::string map_path;
	std::string sheet_path;

public:
	//Load a map from its CSV file and a tilesheet image.
	Map(std::string map_path, std::string sheet_path);

	//Render the whole map into the given window.
	void render_map(Window* active_window);

	//Is this tile coordinate inside the map?
	bool in_bounds(Coordinates proposed_coord);

	/* Dimensions */
	unsigned int get_width();    //in tiles
	unsigned int get_height();   //in tiles
	unsigned int get_tile_width();
	unsigned int get_tile_height();
	unsigned int get_pixel_width();
	unsigned int get_pixel_height();

	//Player spawn tile.
	Coordinates get_start_pos();
};
