#include "SFML/Graphics.hpp"
#include "character.h"

Character::Character()
	: tile(0, 0), facing(DIR::S), anim_phase(0), step_toggle(false),
	  frame_w(16), frame_h(32), loaded(false) {}

Character::Character(int tile_x, int tile_y)
	: tile(tile_x, tile_y), facing(DIR::S), anim_phase(0), step_toggle(false),
	  frame_w(16), frame_h(32), loaded(false) {}

bool Character::load_sprite_sheet(const std::string& path) {
	if (!this->sprite_sheet.loadFromFile(path)) {
		this->loaded = false;
		return false;
	}
	this->sprite_sheet.setSmooth(false);   // crisp pixels when scaled up
	this->current_sprite.setTexture(this->sprite_sheet, true);
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
void Character::set_tile(int x, int y) { this->tile = Coordinates(x, y); }

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
	int tx, ty;
	this->target_tile(dir, tx, ty);
	this->tile = Coordinates(tx < 0 ? 0 : tx, ty < 0 ? 0 : ty);
	// alternate between the two walk frames each step
	this->step_toggle = !this->step_toggle;
	this->anim_phase = this->step_toggle ? 1 : 2;
}

void Character::set_idle() { this->anim_phase = 0; }

/* Rendering */
sf::Sprite* Character::get_current_sprite() { return &this->current_sprite; }

void Character::update_sprite(int tile_px) {
	int frame = this->frame_for(this->facing, this->anim_phase);
	int left = frame * this->frame_w;

	sf::IntRect rect;
	if (this->facing == DIR::E) {
		// horizontal mirror: negative width flips the source rectangle
		rect = sf::IntRect(left + this->frame_w, 0, -this->frame_w, this->frame_h);
	} else {
		rect = sf::IntRect(left, 0, this->frame_w, this->frame_h);
	}
	this->current_sprite.setTextureRect(rect);

	// 16px-wide sprite sits on its tile; 32px height extends one tile upward.
	float px = (float)(this->tile.get_x() * tile_px);
	float py = (float)((int)this->tile.get_y() * tile_px - (this->frame_h - tile_px));
	this->current_sprite.setPosition(px, py);
}
