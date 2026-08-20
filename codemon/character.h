#pragma once
#include "SFML/Graphics.hpp"
#include "direction.h"
#include "Coordinates.h"
#include <string>

/******************************************************************************
Character - a tile-positioned actor drawn from a pokeemerald overworld sheet.

The imported walking sheets (assets/overworld/*.png) are a single row of nine
16x32 frames:
    0 face S   1 face N   2 face W
    3,4 walk S   5,6 walk N   7,8 walk W
East reuses the West frames mirrored horizontally. The sprite is 32px tall but
occupies one 16px tile, so it is drawn shifted up by (frame_h - tile) px, giving
the classic overworld "feet on the tile" look.

The same class is used for the player and for NPCs (construct at a tile and
give it a sheet).
*****************************************************************************/
class Character
{
private:
	sf::Texture sprite_sheet;
	sf::Sprite  current_sprite;

	Coordinates tile;          // position in map tiles
	DIR facing;
	int anim_phase;            // 0 = idle, 1 = step A, 2 = step B
	bool step_toggle;          // alternates the two walk frames

	int frame_w, frame_h;      // 16, 32
	bool loaded;

	int frame_for(DIR dir, int phase) const;

public:
	Character();
	Character(int tile_x, int tile_y);

	bool load_sprite_sheet(const std::string& path =
	                       "assets/overworld/people_brendan_walking.png");

	/* Tile position */
	Coordinates get_tile() const;
	int get_tile_x() const;
	int get_tile_y() const;
	void set_tile(int x, int y);

	// Where this character would end up after a step in `dir` (signed).
	void target_tile(DIR dir, int& out_x, int& out_y) const;

	/* Facing / animation */
	DIR get_facing() const;
	void set_facing(DIR dir);
	void face(DIR dir);            // just turn, no move
	void step(DIR dir);            // turn, advance one tile, toggle walk frame
	void set_idle();

	/* Rendering */
	sf::Sprite* get_current_sprite();
	// Recompute the source rectangle and on-screen position; tile_px is the
	// map's metatile size (16).
	void update_sprite(int tile_px);
};
