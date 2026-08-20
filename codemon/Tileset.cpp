#include "Tileset.h"

Tileset::Tileset()
	: metatile_px(16), cols(0), count(0), loaded(false) {}

bool Tileset::load(const std::string& sheet_path, int metatile_size) {
	if (!this->sheet.loadFromFile(sheet_path)) {
		this->loaded = false;
		return false;
	}
	this->sheet.setSmooth(false);   // crisp pixels when the view is scaled up
	this->metatile_px = metatile_size;
	sf::Vector2u dim = this->sheet.getSize();
	this->cols = (metatile_size > 0) ? (int)(dim.x / metatile_size) : 0;
	int rows = (metatile_size > 0) ? (int)(dim.y / metatile_size) : 0;
	this->count = this->cols * rows;
	this->loaded = true;
	return true;
}

bool Tileset::is_loaded() const { return this->loaded; }
int Tileset::size() const { return this->metatile_px; }
int Tileset::metatile_count() const { return this->count; }
const sf::Texture& Tileset::texture() const { return this->sheet; }

sf::IntRect Tileset::source_rect(int id) const {
	if (this->cols <= 0 || id < 0) {
		return sf::IntRect(0, 0, this->metatile_px, this->metatile_px);
	}
	int sx = (id % this->cols) * this->metatile_px;
	int sy = (id / this->cols) * this->metatile_px;
	return sf::IntRect(sx, sy, this->metatile_px, this->metatile_px);
}
