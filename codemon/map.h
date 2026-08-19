#pragma once
#include <string>
#include <fstream>

#include "Tile.h"
#include "Window.h"
#include "SFML/Graphics.hpp"
#include "direction.h"
#include "character.h"
#include "Coordinates.h"



class Map
{
private:
	//Tilesize definitions
	Coordinates tile_dimensions;

	//How big the map is in x, y
	Coordinates dimensions;
	//Where the player character spawns when loaded
	Coordinates start_pos;

	//Tilesheet for the map
	sf::Texture map_sheet;

	//Path to map file that can read path
	std::string map_path;
	std::string sheet_path;
	//2d array of tile pointers
	Tile* tile_map;

	//Put a tile into the map, tile holds its own pos but 
	//the new tile is also indexed by its pos.
	void add_tile(Tile *new_tile);

	void set_tile_size();
	int get_tile_width();
	int get_tile_height();

public:
	//get a pointer to the map
	Tile* get_map();
	//Load a given map from its bin map and a tilesheet
	Map(std::string map_path, std::string sheet_path);
	//Into a given window render this map
	void render_map(Window *active_window);
	//Will a character and a direction result in a valid move on this map
	bool in_bounds(Character *moving_char, DIR move_dir);
	//Is this coordinate within this map
	bool in_bounds(Coordinates proposed_coord);

	/* Dimensionality setters */
	void set_width(unsigned int width);
	void set_height(unsigned int height);
	void set_dimensions(Coordinates start);

	/* Dimensionality Getters*/
	unsigned int get_width();
	unsigned int get_height();
	void set_start_pos(Coordinates start);

	/* Initializer for the the size, in two dimensions, of each tile in the map. 
	its the size of a generic tile for this map in pixels.*/
	void set_tile_size(unsigned int tile_width, unsigned int tile_height);
	void set_tile_size(Coordinates tile_dimensionality);


	//Get dimensions as a tuple
	Coordinates *get_dimensions();

	//Get player start position
	Coordinates get_start_pos();

};