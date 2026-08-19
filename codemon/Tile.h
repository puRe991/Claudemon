#pragma once
#include "Coordinates.h"

class Tile
{
public:
	// Terrain vocabulary for the region.
	//
	// short_grass (0) and long_grass (1) keep their original ids so the
	// legacy map_00 tile sheet and the existing tests stay valid. The higher
	// ids give the region maps enough distinct terrain to express every
	// described landscape: coastal villages, forests, mountains, cave systems,
	// water routes, cities and building interiors. Each id is an index into a
	// tile sheet row (see Map::render_map), so keep new tiles contiguous.
	enum tile {
		short_grass = 0,   // open, walkable grass / plains
		long_grass  = 1,   // tall wild grass (encounters)
		path        = 2,   // roads, town paths, dirt routes
		water       = 3,   // sea, rivers, lakes
		sand        = 4,   // beaches, coastlines
		tree        = 5,   // forest trees / town foliage (blocking)
		rock        = 6,   // mountain walls, boulders (blocking)
		flower      = 7,   // flowerbeds, gardens
		building    = 8,   // town building footprints (blocking)
		floor       = 9,   // interior floor
		wall        = 10,  // interior wall (blocking)
		cave_floor  = 11,  // cave / tunnel floor
		cave_wall   = 12,  // cave / tunnel wall (blocking)
		ledge       = 13,  // one-way ledges / cliffs
		bridge      = 14,  // bridges over water
		stairs      = 15,  // stairs between cave/building levels
		warp        = 16,  // doors, cave mouths, area transitions
		sign        = 17,  // signs / points of interest
		ice         = 18,  // frozen cave floor (Seafoam)
		lava        = 19,  // volcanic terrain (Cinnabar)
		deep_water  = 20   // open ocean / sea routes
	};

	/* Constructors */
	Tile();
	Tile(unsigned int x, unsigned int y, Tile::tile data);
	Tile(Coordinates* tile_pos, Tile::tile data);
	
	/* Coordinate Getters*/
	unsigned int get_x();
	unsigned int get_y();
	Coordinates* get_pos();

	//Change and set tile position coordinate 
	void set_pos(unsigned int x, unsigned int y);

	/* Get what type of tile */
	Tile::tile get_data();

private:
	Coordinates *pos;
	Tile::tile data;
};

