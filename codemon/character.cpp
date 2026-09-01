#include "SFML/Graphics.hpp"
#include "character.h"

Character::Character()
	: tile(0, 0), prev_tile(0, 0), move_t(1.f), animated(true), running(false),
	  facing(DIR::S), anim_phase(0), step_toggle(false),
	  frame_w(16), frame_h(32), frame_count(9), directional(true), loaded(false) {}

Character::Character(int tile_x, int tile_y)
	: tile(tile_x, tile_y), prev_tile(tile_x, tile_y), move_t(1.f), animated(true), running(false),
	  facing(DIR::S), anim_phase(0), step_toggle(false),
	  frame_w(16), frame_h(32), frame_count(9), directional(true), loaded(false) {}

// Overworld sheets whose frames are 32px wide rather than the usual 16px
// (pokeemerald's ObjectEventGraphicsInfo .width for these graphics is 32).
// Everything not listed here is inferred from the sheet's dimensions in
// load_sprite_sheet() below.
// "assets/overworld/misc_item_ball.png" -> "misc_item_ball"
static std::string sheet_stem(const std::string& path) {
	std::string::size_type slash = path.find_last_of("/\\");
	std::string stem = (slash == std::string::npos) ? path : path.substr(slash + 1);
	std::string::size_type dot = stem.find_last_of('.');
	return (dot == std::string::npos) ? stem : stem.substr(0, dot);
}

// Whether this sheet's frames are facings (an ordinary character) or states.
// pokeemerald picks per graphics id: the character sheets use
// sAnimTable_Standard and friends, while item balls, statues, the truck and
// the cable car are sAnimTable_Inanimate, and rocks, cut trees and berry
// trees have their own tables whose frames are break/cut/growth stages.
// Turning one of those "towards the player" (which is what talking to it
// does) would otherwise pick a half-smashed rock or a younger tree.
static bool directional_sheet(const std::string& stem) {
	static const char* STATE_FRAMES[] = {
		"misc_birchs_bag", "misc_birth_island_stone", "misc_breakable_rock",
		"misc_cable_car", "misc_cuttable_tree", "misc_fossil", "misc_item_ball",
		"misc_moving_box", "misc_pushable_boulder", "misc_statue", "misc_truck",
		"people_brendan_decorating", "people_may_decorating", "people_nurse",
		"pokemon_rayquaza", "pokemon_rayquaza_still",
	};
	if (stem.rfind("berry_trees_", 0) == 0) return false;   // growth stages
	if (stem.rfind("dolls_", 0) == 0) return false;         // decorations
	if (stem.rfind("cushions_", 0) == 0) return false;
	for (const char* n : STATE_FRAMES) if (stem == n) return false;
	return true;
}

static bool wide_frame_sheet(const std::string& path) {
	static const char* WIDE[] = {
		"misc_birth_island_stone", "misc_mr_brineys_boat",
		"people_brendan_acro_bike", "people_brendan_field_move",
		"people_brendan_fishing", "people_brendan_mach_bike",
		"people_brendan_surfing", "people_brendan_underwater",
		"people_brendan_watering", "people_cycling_triathlete_f",
		"people_cycling_triathlete_m", "people_may_acro_bike",
		"people_may_field_move", "people_may_fishing",
		"people_may_mach_bike", "people_may_surfing",
		"people_may_underwater", "people_may_watering",
		"people_quinty_plump", "pokemon_deoxys", "pokemon_enemy_zigzagoon",
		"pokemon_ho_oh", "pokemon_lugia", "pokemon_poochyena",
	};
	for (const char* w : WIDE) if (sheet_stem(path) == w) return true;
	return false;
}

bool Character::load_sprite_sheet(const std::string& path) {
	if (!this->sprite_sheet.loadFromFile(path)) {
		this->loaded = false;
		return false;
	}
	this->sprite_sheet.setSmooth(false);   // crisp pixels when scaled up
	this->current_sprite.setTexture(this->sprite_sheet, true);

	// Work out this sheet's frame layout instead of assuming the ordinary
	// 144x32 (9 frames of 16x32) people sheet. The imported overworld
	// graphics are far from uniform: item balls are a single 16x16 frame,
	// small NPCs (ninja boy, tuber) are 16x16, gym leaders and Pokemon are
	// often 3 frames with no walk cycle, and set pieces (the moving truck,
	// the cable car, Rayquaza, the S.S. Tidal) are whole multi-tile images.
	// Reading a fixed 16x32 rect out of those sampled past the end of the
	// texture and drew them one tile too high.
	sf::Vector2u dim = this->sprite_sheet.getSize();
	int tw = (int)dim.x, th = (int)dim.y;
	if (tw <= 0 || th <= 0) { this->loaded = false; return false; }
	this->directional = directional_sheet(sheet_stem(path));
	if (th <= 32 && wide_frame_sheet(path)) {
		// Two tiles wide: a 96x32 sheet like Mr. Briney's boat is three
		// 32x32 frames, not six 16x32 ones. The sheet's dimensions alone
		// can't tell the two apart, so these follow pokeemerald's own
		// .width/.height in src/data/object_events/object_event_graphics_info.h.
		this->frame_w = 32;
		this->frame_h = th;
	} else if (th <= 32 && tw % 16 == 0) {
		// Ordinary character sheet: a row of 16px-wide frames.
		this->frame_w = 16;
		this->frame_h = th;
	} else if (th > 32 && tw % th == 0) {
		// Large square set piece, possibly several frames side by side
		// (Rayquaza's 320x64 is five 64x64 frames).
		this->frame_w = th;
		this->frame_h = th;
	} else {
		// Anything else is one indivisible image (submarine shadow, S.S.
		// Tidal, ...).
		this->frame_w = tw;
		this->frame_h = th;
	}
	this->frame_count = tw / this->frame_w;
	if (this->frame_count < 1) this->frame_count = 1;

	this->loaded = true;
	this->update_sprite(16);
	return true;
}

// Map (facing, phase) onto the 9-frame single-row sheet layout.
int Character::frame_for(DIR dir, int phase) const {
	switch (dir) {
	case DIR::N: return phase == 0 ? 1 : (phase == 1 ? 5 : 6);
	case DIR::W: return phase == 0 ? 2 : (phase == 1 ? 7 : 8);
	case DIR::E: return phase == 0 ? 2 : (phase == 1 ? 7 : 8); // mirrored at draw
	case DIR::S:
	default:     return phase == 0 ? 0 : (phase == 1 ? 3 : 4);
	}
}

/* Tile position */
Coordinates Character::get_tile() const { return this->tile; }
int Character::get_tile_x() const { return (int)this->tile.get_x(); }
int Character::get_tile_y() const { return (int)this->tile.get_y(); }
void Character::set_tile(int x, int y) {
	this->tile = Coordinates(x, y);
	this->prev_tile = this->tile;
	this->move_t = 1.f;   // teleport (warp/connection), never a slide
}

void Character::target_tile(DIR dir, int& out_x, int& out_y) const {
	out_x = (int)this->tile.get_x();
	out_y = (int)this->tile.get_y();
	switch (dir) {
	case DIR::N: out_y -= 1; break;
	case DIR::S: out_y += 1; break;
	case DIR::E: out_x += 1; break;
	case DIR::W: out_x -= 1; break;
	default: break;
	}
}

/* Facing / animation */
DIR Character::get_facing() const { return this->facing; }
void Character::set_facing(DIR dir) { if (dir != DIR::NONE) this->facing = dir; }
void Character::face(DIR dir) { this->set_facing(dir); this->anim_phase = 0; }

void Character::step(DIR dir) {
	if (dir == DIR::NONE) return;
	this->facing = dir;
	if (this->animated) this->prev_tile = this->tile;
	int tx, ty;
	this->target_tile(dir, tx, ty);
	this->tile = Coordinates(tx < 0 ? 0 : tx, ty < 0 ? 0 : ty);
	this->move_t = this->animated ? 0.f : 1.f;
	// alternate between the two walk frames each step
	this->step_toggle = !this->step_toggle;
	this->anim_phase = this->step_toggle ? 1 : 2;
}

void Character::jump(DIR dir) {
	if (dir == DIR::NONE) return;
	this->facing = dir;
	if (this->animated) this->prev_tile = this->tile;
	int mx, my;   // the tile jumped over
	this->target_tile(dir, mx, my);
	int lx = mx, ly = my;
	switch (dir) {
	case DIR::N: ly -= 1; break;
	case DIR::S: ly += 1; break;
	case DIR::E: lx += 1; break;
	case DIR::W: lx -= 1; break;
	default: break;
	}
	this->tile = Coordinates(lx < 0 ? 0 : lx, ly < 0 ? 0 : ly);
	this->move_t = this->animated ? 0.f : 1.f;
	this->step_toggle = !this->step_toggle;
	this->anim_phase = this->step_toggle ? 1 : 2;
}

void Character::set_idle() { this->anim_phase = 0; }

// Real Pokemon-game walk speed is ~2px/frame at 60fps for a 16px tile, i.e.
// about 8 frames -- this just needs to be a bit under MOVE_INTERVAL (see
// main.cpp) so one slide always finishes before the next one can start.
// Running (Running Shoes, held Shift) is the real games' exact 2x walk
// speed, paired with main.cpp's halved RUN_MOVE_INTERVAL the same way.
static const float MOVE_DURATION = 0.13f;
static const float RUN_MOVE_DURATION = MOVE_DURATION / 2.f;

void Character::tick(float dt) {
	if (this->move_t < 1.f) {
		float duration = this->running ? RUN_MOVE_DURATION : MOVE_DURATION;
		this->move_t += dt / duration;
		if (this->move_t > 1.f) this->move_t = 1.f;
	}
}

/* Rendering */
sf::Sprite* Character::get_current_sprite() { return &this->current_sprite; }

float Character::interp_x(int tile_px) const {
	float tx = (float)(this->tile.get_x() * tile_px);
	if (this->move_t >= 1.f) return tx;
	float fx = (float)(this->prev_tile.get_x() * tile_px);
	return fx + (tx - fx) * this->move_t;
}

float Character::interp_y(int tile_px) const {
	float ty = (float)(this->tile.get_y() * tile_px);
	if (this->move_t >= 1.f) return ty;
	float fy = (float)(this->prev_tile.get_y() * tile_px);
	return fy + (ty - fy) * this->move_t;
}

void Character::update_sprite(int tile_px) {
	// A sheet may hold fewer frames than the 9-frame layout addresses (three
	// facings with no walk cycle, or a single static image). Fall back to the
	// idle frame for this facing, then to frame 0, rather than sampling past
	// the edge of the texture.
	int frame = this->directional ? this->frame_for(this->facing, this->anim_phase) : 0;
	if (frame >= this->frame_count) frame = this->frame_for(this->facing, 0);
	if (frame >= this->frame_count) frame = 0;
	int left = frame * this->frame_w;

	sf::IntRect rect;
	if (this->facing == DIR::E && this->directional) {
		// horizontal mirror: negative width flips the source rectangle
		rect = sf::IntRect(left + this->frame_w, 0, -this->frame_w, this->frame_h);
	} else {
		rect = sf::IntRect(left, 0, this->frame_w, this->frame_h);
	}
	this->current_sprite.setTextureRect(rect);

	// The sprite stands on its tile: anything taller than one tile extends
	// upward, anything wider is centred on the tile (multi-tile set pieces).
	float px = this->interp_x(tile_px) - (float)(this->frame_w - tile_px) / 2.f;
	float py = this->interp_y(tile_px) - (float)(this->frame_h - tile_px);
	this->current_sprite.setPosition(px, py);
}
