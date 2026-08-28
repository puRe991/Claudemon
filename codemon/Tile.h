#pragma once
#include "Coordinates.h"

class Tile
{
public:
	// Legacy named ids kept for the unit tests. A tile now stores an
	// arbitrary metatile id (an index into a Tileset sheet), so `data` is an
	// int and these enum values just seed the two original demo tiles.
	enum tile { long_grass = 1, short_grass = 0 };

	/* Constructors */
	Tile();
	Tile(unsigned int x, unsigned int y, int data);
	Tile(Coordinates* tile_pos, int data);
	
	/* Coordinate Getters*/
	unsigned int get_x();
	unsigned int get_y();
	Coordinates* get_pos();

	//Change and set tile position coordinate 
	void set_pos(unsigned int x, unsigned int y);

	/* Get the metatile id stored in this tile */
	int get_data();

private:
	Coordinates *pos;
	int data;
};

