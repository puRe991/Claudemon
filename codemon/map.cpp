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
		       s.rfind("npcs", 0) == 0 || s.rfind("dialogs", 0) == 0 ||
		       s.rfind("signs", 0) == 0 || s.rfind("grass", 0) == 0 ||
		       s.rfind("encounters", 0) == 0 || s.rfind("objscripts", 0) == 0 ||
		       s.rfind("triggers", 0) == 0 || s.rfind("movements", 0) == 0 ||
		       s.rfind("scriptdefs", 0) == 0;
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
			// "<sheet_key> <x> <y> <S|N|E|W> <static|wander|pace_v|pace_h> [hide_flag]"
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				std::stringstream ss(rest[i]);
				NpcSpawn n;
				std::string face, move, flag;
				if (!(ss >> n.sheet >> n.x >> n.y >> face >> move)) continue;
				n.facing = (face == "N") ? DIR::N : (face == "E") ? DIR::E :
				           (face == "W") ? DIR::W : DIR::S;
				n.movement = (move == "wander") ? MOVE_WANDER :
				             (move == "pace_v") ? MOVE_PACE_V :
				             (move == "pace_h") ? MOVE_PACE_H : MOVE_STATIC;
				if (ss >> flag && flag != "-") n.hide_flag = flag;
				this->npc_spawns.push_back(n);
			}
		} else if (head.rfind("dialogs", 0) == 0) {
			// "<npc_index>\t<text>"
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				size_t tab = rest[i].find('\t');
				if (tab == std::string::npos) continue;
				int idx = std::stoi(rest[i].substr(0, tab));
				std::string text = rest[i].substr(tab + 1);
				if (idx >= 0 && idx < (int)this->npc_spawns.size())
					this->npc_spawns[idx].dialog = text;
			}
		} else if (head.rfind("signs", 0) == 0) {
			// "<x> <y>\t<text>"
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				size_t tab = rest[i].find('\t');
				if (tab == std::string::npos) continue;
				std::stringstream ss(rest[i].substr(0, tab));
				Sign sg;
				if (!(ss >> sg.x >> sg.y)) continue;
				sg.text = rest[i].substr(tab + 1);
				this->sign_list.push_back(sg);
			}
		} else if (head.rfind("grass", 0) == 0) {
			// single line of comma-separated grass metatile ids
			if (++i < rest.size()) {
				std::vector<int> g;
				parse_int_row(rest[i], g);
				for (int id : g) this->grass_ids.insert(id);
				++i;
			}
		} else if (head.rfind("encounters", 0) == 0) {
			// lines like "land SP:min:max,SP:min:max,...". Only land triggers
			// in tall grass; other types are parsed but ignored for now.
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].rfind("land ", 0) != 0) continue;
				std::stringstream ss(rest[i].substr(5));
				std::string entry;
				while (std::getline(ss, entry, ',')) {
					size_t a = entry.find(':'), b = entry.rfind(':');
					if (a == std::string::npos || b == a) continue;
					EncSlot s;
					s.species = entry.substr(0, a);
					s.min_level = std::stoi(entry.substr(a + 1, b - a - 1));
					s.max_level = std::stoi(entry.substr(b + 1));
					this->land_slots.push_back(s);
				}
			}
		} else if (head.rfind("objscripts", 0) == 0) {
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				std::stringstream ss(rest[i]);
				int idx; std::string lab;
				if (ss >> idx >> lab) this->npc_script_map[idx] = lab;
			}
		} else if (head.rfind("triggers", 0) == 0) {
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				std::stringstream ss(rest[i]);
				ScriptTrigger t;
				if (ss >> t.x >> t.y >> t.var >> t.val >> t.label)
					this->script_triggers.push_back(t);
			}
		} else if (head.rfind("movements", 0) == 0) {
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				if (rest[i].empty()) continue;
				size_t tab = rest[i].find('\t');
				if (tab == std::string::npos) continue;
				std::string lab = rest[i].substr(0, tab);
				std::vector<std::string> acts;
				std::stringstream ss(rest[i].substr(tab + 1));
				std::string a;
				while (std::getline(ss, a, ',')) if (!a.empty()) acts.push_back(a);
				this->move_defs[lab] = acts;
			}
		} else if (head.rfind("scriptdefs", 0) == 0) {
			std::string cur;
			for (++i; i < rest.size() && !is_keyword(rest[i]); ++i) {
				const std::string& l = rest[i];
				if (l.empty()) continue;
				if (l.rfind("= ", 0) == 0) {
					cur = l.substr(2);
					this->script_defs[cur];              // create empty
				} else if (!cur.empty()) {
					Instr instr;
					if (l.rfind("msgbox\t", 0) == 0) {
						instr.push_back("msgbox");
						instr.push_back(l.substr(7));    // text (may contain \x1f)
					} else {
						std::stringstream ss(l);
						std::string tok;
						while (ss >> tok) instr.push_back(tok);
					}
					if (!instr.empty()) this->script_defs[cur].push_back(instr);
				}
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

const Sign* Map::sign_at(int tile_x, int tile_y) const {
	for (const Sign& s : this->sign_list) {
		if (s.x == tile_x && s.y == tile_y) return &s;
	}
	return nullptr;
}

bool Map::is_grass(int tile_x, int tile_y) const {
	if (!in_bounds(tile_x, tile_y) || this->grass_ids.empty()) return false;
	int id = this->tile_map[this->index(tile_x, tile_y)];
	return this->grass_ids.count(id) > 0;
}

bool Map::has_encounters() const { return !this->land_slots.empty(); }

bool Map::encounter_here(int x, int y) const {
	if (this->land_slots.empty()) return false;
	if (is_grass(x, y)) return true;
	// Cave / indoor encounter map: has land encounters but no tall-grass tiles
	// -> the whole walkable floor triggers (excluding warp/door tiles).
	if (this->grass_ids.empty() && passable(x, y) && !warp_at(x, y))
		return true;
	return false;
}

bool Map::roll_encounter(std::mt19937& rng, std::string& species, int& level) const {
	if (this->land_slots.empty()) return false;
	// Standard Gen-3 land slot rates (percent) for the 12 slots.
	static const int RATE[12] = {20, 20, 10, 10, 10, 10, 5, 5, 5, 4, 1, 1};
	int n = (int)this->land_slots.size();
	int total = 0;
	for (int i = 0; i < n && i < 12; ++i) total += RATE[i];
	if (total <= 0) total = n;
	int r = (int)(rng() % total);
	int idx = 0;
	for (int i = 0; i < n; ++i) {
		int w = (i < 12) ? RATE[i] : 1;
		if (r < w) { idx = i; break; }
		r -= w;
	}
	const EncSlot& s = this->land_slots[idx];
	int lo = s.min_level, hi = s.max_level;
	if (hi < lo) hi = lo;
	level = lo + (int)(rng() % (hi - lo + 1));
	species = s.species;
	return true;
}

bool Map::has_script(const std::string& label) const {
	return this->script_defs.count(label) > 0;
}

const std::vector<Instr>& Map::script(const std::string& label) const {
	static const std::vector<Instr> empty;
	auto it = this->script_defs.find(label);
	return it == this->script_defs.end() ? empty : it->second;
}

const std::vector<std::string>& Map::movement(const std::string& label) const {
	static const std::vector<std::string> empty;
	auto it = this->move_defs.find(label);
	return it == this->move_defs.end() ? empty : it->second;
}

std::string Map::npc_script(int npc_index) const {
	auto it = this->npc_script_map.find(npc_index);
	return it == this->npc_script_map.end() ? std::string() : it->second;
}

const ScriptTrigger* Map::trigger_at(int tile_x, int tile_y) const {
	for (const ScriptTrigger& t : this->script_triggers) {
		if (t.x == tile_x && t.y == tile_y) return &t;
	}
	return nullptr;
}

bool Map::set_metatile(int tile_x, int tile_y, int id, bool impassable) {
	if (!in_bounds(tile_x, tile_y)) return false;
	this->tile_map[this->index(tile_x, tile_y)] = id;
	this->solid[this->index(tile_x, tile_y)] = impassable ? 1 : 0;
	return true;
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
