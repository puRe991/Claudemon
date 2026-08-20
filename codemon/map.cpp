#include "map.h"
#include <fstream>
#include <sstream>
#include <iostream>

// Parse one comma-separated row of ints into `out`.
static void parse_int_row(const std::string& line, std::vector<int>& out) {
	std::stringstream ss(line);
	std::string cell;
	while (std::getline(ss, cell, ',')) {
		if (cell.empty()) continue;
		out.push_back(std::stoi(cell));
	}
}

int Map::index(int x, int y) const {
	return y * (int)this->dimensions.get_x() + x;
}

Map::Map(const std::string& map_path, const std::string& tileset_dir)
	: tile_px(16) {
	std::ifstream f(map_path);
	if (!f.is_open()) {
		std::cerr << "Map: could not open " << map_path << "\n";
		return;
	}

	std::string line;
	std::string tileset_name = "general";

	// Line 1: "tileset <name>"
	if (std::getline(f, line)) {
		std::stringstream ss(line);
		std::string key;
		ss >> key >> tileset_name;
	}
	// Line 2: "W H"
	unsigned int w = 0, h = 0;
	if (std::getline(f, line)) {
		std::stringstream ss(line);
		ss >> w >> h;
	}
	this->dimensions = Coordinates(w, h);
	// Line 3: "startx starty"
	unsigned int sx = 0, sy = 0;
	if (std::getline(f, line)) {
		std::stringstream ss(line);
		ss >> sx >> sy;
	}
	this->start_pos = Coordinates(sx, sy);

	// Load the coloured tileset sheet and adopt its metatile size.
	if (this->tileset.load(tileset_dir + "/" + tileset_name + ".png")) {
		this->tile_px = this->tileset.size();
	}

	// Metatile layer: exactly `h` rows.
	this->tile_map.reserve((size_t)w * h);
	for (unsigned int row = 0; row < h && std::getline(f, line); ++row) {
		parse_int_row(line, this->tile_map);
	}

	// Remaining lines hold optional sections ("collision", "warps", "npcs")
	// in any order. Read them all, then dispatch by keyword so a section stops
	// cleanly at the next keyword.
	this->solid.assign((size_t)w * h, 0);
	std::vector<std::string> rest;
	while (std::getline(f, line)) rest.push_back(line);

	auto is_keyword = [](const std::string& s) {
		return s.rfind("collision", 0) == 0 || s.rfind("warps", 0) == 0 ||
		       s.rfind("npcs", 0) == 0;
	};

	for (size_t i = 0; i < rest.size(); ) {
		const std::string& head = rest[i];
		if (head.rfind("collision", 0) == 0) {
			std::vector<int> flags;
			++i;
			for (unsigned int row = 0; row < h && i < rest.size(); ++row, ++i) {
				parse_int_row(rest[i], flags);
			}
			for (size_t k = 0; k < flags.size() && k < this->solid.size(); ++k) {
				this->solid[k] = flags[k] ? 1 : 0;
			}
		} else if (head.rfind("warps", 0) == 0) {
			// "<x> <y> <dest_map_folder> <dest_warp_id>"
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				std::stringstream ss(rest[i]);
				Warp wp;
				if (!(ss >> wp.x >> wp.y >> wp.dest >> wp.dest_warp)) continue;
				this->warp_list.push_back(wp);
			}
		} else if (head.rfind("npcs", 0) == 0) {
			// "<sheet_key> <x> <y> <S|N|E|W> <static|wander|pace_v|pace_h>"
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				std::stringstream ss(rest[i]);
				NpcSpawn n;
				std::string face, move;
				if (!(ss >> n.sheet >> n.x >> n.y >> face >> move)) continue;
				n.facing = (face == "N") ? DIR::N : (face == "E") ? DIR::E :
				           (face == "W") ? DIR::W : DIR::S;
				n.movement = (move == "wander") ? MOVE_WANDER :
				             (move == "pace_v") ? MOVE_PACE_V :
				             (move == "pace_h") ? MOVE_PACE_H : MOVE_STATIC;
				this->npc_spawns.push_back(n);
			}
		} else {
			++i;
		}
	}
}

const std::vector<NpcSpawn>& Map::npcs() const { return this->npc_spawns; }
const std::vector<Warp>& Map::warps() const { return this->warp_list; }

const Warp* Map::warp_at(int tile_x, int tile_y) const {
	for (const Warp& w : this->warp_list) {
		if (w.x == tile_x && w.y == tile_y) return &w;
	}
	return nullptr;
}

const Warp* Map::warp_by_index(int idx) const {
	if (idx < 0 || idx >= (int)this->warp_list.size()) return nullptr;
	return &this->warp_list[idx];
}

bool Map::ready() const {
	return this->tileset.is_loaded() &&
	       this->tile_map.size() == (size_t)this->dimensions.get_x() * this->dimensions.get_y() &&
	       !this->tile_map.empty();
}

void Map::render_map(Window* active_window) {
	this->render_to(*active_window->get_window());
}

void Map::render_to(sf::RenderTarget& target) {
	if (!this->tileset.is_loaded()) return;

	sf::Sprite to_draw(this->tileset.texture());
	int w = (int)this->dimensions.get_x();
	int h = (int)this->dimensions.get_y();

	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int id = this->tile_map[this->index(x, y)];
			to_draw.setTextureRect(this->tileset.source_rect(id));
			to_draw.setPosition((float)(x * this->tile_px),
			                    (float)(y * this->tile_px));
			target.draw(to_draw);
		}
	}
}

bool Map::in_bounds(int tile_x, int tile_y) const {
	return tile_x >= 0 && tile_y >= 0 &&
	       tile_x < (int)this->dimensions.get_x() &&
	       tile_y < (int)this->dimensions.get_y();
}

bool Map::in_bounds(Coordinates proposed_coord) const {
	return in_bounds((int)proposed_coord.get_x(), (int)proposed_coord.get_y());
}

bool Map::passable(int tile_x, int tile_y) const {
	if (!in_bounds(tile_x, tile_y)) return false;
	return this->solid[this->index(tile_x, tile_y)] == 0;
}

unsigned int Map::get_width() const { return this->dimensions.get_x(); }
unsigned int Map::get_height() const { return this->dimensions.get_y(); }
int Map::get_tile_size() const { return this->tile_px; }
Coordinates Map::get_start_pos() const { return this->start_pos; }
