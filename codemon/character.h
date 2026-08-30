#pragma once
#include "SFML/Graphics.hpp"
#include "direction.h"
#include "Coordinates.h"
#include <string>

/******************************************************************************
Character - a tile-positioned actor drawn from a pokeemerald overworld sheet.

The imported walking sheets (assets/overworld/<sheet>.png) are a single row of nine
16x32 frames:
    0 face S   1 face N   2 face W
    3-4 walk S   5-6 walk N   7-8 walk W
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

	Coordinates tile;          // logical position in map tiles (authoritative
	                           // for collision/warps/scripts -- updates the
	                           // instant a step is taken)
	Coordinates prev_tile;     // tile the current slide animation started from
	float move_t;              // 0..1 slide progress from prev_tile to tile;
	                           // >=1 means fully arrived (no animation pending)
	bool animated;             // false -> step() snaps instantly, no slide
	                           // (headless/screenshot mode: deterministic,
	                           // frame-exact rendering, no real-time tick())
	bool running;              // Running Shoes (FLAG_SYS_B_DASH) held down:
	                           // tick() slides at RUN_MOVE_DURATION instead
	                           // of MOVE_DURATION, matching main.cpp's
	                           // shortened RUN_MOVE_INTERVAL between steps.
	bool surfing = false;      // true once the player has confirmed Surf onto
	                           // water (main.cpp's Surf gate); dismounts
	                           // automatically on reaching a non-water tile.
	DIR facing;
	int anim_phase;            // 0 = idle, 1 = step A, 2 = step B
	bool step_toggle;          // alternates the two walk frames

	int frame_w, frame_h;      // 16, 32
	bool loaded;

	std::string hide_flag;     // this NPC's pokeemerald FLAG_HIDE_*/FLAG_TEMP_*
	                           // (empty for the player and permanent NPCs);
	                           // `removeobject` (cut trees, ...) sets it so the
	                           // object stays gone across a map reload.
	// Runtime movement behaviour, mirrors map.h's MoveKind (0=static,
	// 1=wander, 2=pace vertical, 3=pace horizontal) without needing map.h's
	// full include here. Initialized from the NpcSpawn this object was built
	// from; `setobjectmovementtype` (ScriptVM) can change it live.
	int move_kind = 0;
	// `setobjectsubpriority`/`resetobjectsubpriority`: draw-order tiebreak for
	// two objects sharing a tile-y (Mr. Briney's boat: the rider must draw
	// above the boat while boarding, but the reverse while the boat glides
	// past land at the same row). -1 = unset -- tile-y order decides alone,
	// same as before this opcode was wired up; when set, it's the tiebreak
	// among objects that share the exact same tile-y (lower value = drawn
	// first = appears behind).
	int subpriority = -1;
	bool removed = false;      // `removeobject` also sets this so the object's
	                           // still-alive Agent entry (main.cpp's `agents`,
	                           // separate from the `actors` render/collision
	                           // list ScriptVM already drops it from) can't be
	                           // interacted with again this same session.

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
	void set_tile(int x, int y);   // teleport (warp/connection): snaps, no slide

	// Headless/screenshot mode wants exact, deterministic tile-snapped
	// rendering (nothing calls tick(), so a pending slide would just freeze
	// the sprite at its old tile forever); real interactive play wants the
	// smooth slide. Defaults to animated.
	void set_animated(bool a) { this->animated = a; }

	// PC control adaptation of the GBA's "hold B to run": here it's held
	// Shift, gated on FLAG_SYS_B_DASH (see main.cpp). Only meaningful while
	// animated -- headless mode never calls tick() so it has no effect there.
	void set_running(bool r) { this->running = r; }

	void set_surfing(bool s) { this->surfing = s; }
	bool is_surfing() const { return this->surfing; }

	void set_hide_flag(const std::string& f) { this->hide_flag = f; }
	const std::string& get_hide_flag() const { return this->hide_flag; }
	void mark_removed() { this->removed = true; }
	bool is_removed() const { return this->removed; }

	void set_move_kind(int k) { this->move_kind = k; }
	int get_move_kind() const { return this->move_kind; }

	void set_subpriority(int p) { this->subpriority = p; }
	void reset_subpriority() { this->subpriority = -1; }
	int get_subpriority() const { return this->subpriority; }

	// Where this character would end up after a step in `dir` (signed).
	void target_tile(DIR dir, int& out_x, int& out_y) const;

	/* Facing / animation */
	DIR get_facing() const;
	void set_facing(DIR dir);
	void face(DIR dir);            // just turn, no move
	void step(DIR dir);            // turn, advance one tile, toggle walk frame
	// Ledge hop (pokeemerald's DoLedgeJump): slide two tiles in `dir` in one
	// continuous animation instead of a normal one-tile step. Bypasses
	// collision on the tile jumped over -- the caller (player_step in
	// main.cpp) has already verified the landing tile is clear.
	void jump(DIR dir);
	void set_idle();
	// Advance the current slide animation by `dt` seconds. Interactive-loop
	// only -- headless code never calls this (see `animated`/set_animated).
	void tick(float dt);
	bool is_moving() const { return this->move_t < 1.f; }

	/* Rendering */
	sf::Sprite* get_current_sprite();
	// Recompute the source rectangle and on-screen position; tile_px is the
	// map's metatile size (16).
	void update_sprite(int tile_px);
	// Interpolated on-screen pixel position of the tile origin (mid-slide
	// while animating, else the same as tile*tile_px); the camera uses this
	// too so it glides along with the sprite instead of snapping per tile.
	float interp_x(int tile_px) const;
	float interp_y(int tile_px) const;
};
