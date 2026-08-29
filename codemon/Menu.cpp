#include "Menu.h"
#include <cctype>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

Menu::Menu() : font_ok(false), screen(CLOSED), cursor(0),
               bag_cursor(0), teach_cursor(0), use_cursor(0), fly_cursor(0),
               gs(nullptr), bdata(nullptr), team(nullptr), box(nullptr),
               saved_teams(nullptr), cursor_ok(false) {}

// Fixed Fly destinations (pokeemerald's canFly town/city list, region_map.c's
// GetMapSecType switch): flag -> (map file stem, arrival tile), the same
// tile each town's own heal location uses (src/data/heal_locations.json),
// since flying lands you at the same spot recovering from a whiteout would.
struct FlyDest { const char* flag; const char* map; const char* name; int x, y; };
static const FlyDest FLY_DESTINATIONS[] = {
	{"FLAG_VISITED_LITTLEROOT_TOWN", "LittlerootTown",  "Littleroot Town",  5,  9},
	{"FLAG_VISITED_OLDALE_TOWN",     "OldaleTown",      "Oldale Town",       6, 17},
	{"FLAG_VISITED_DEWFORD_TOWN",    "DewfordTown",     "Dewford Town",      2, 11},
	{"FLAG_VISITED_LAVARIDGE_TOWN",  "LavaridgeTown",   "Lavaridge Town",    9,  7},
	{"FLAG_VISITED_FALLARBOR_TOWN",  "FallarborTown",   "Fallarbor Town",   14,  8},
	{"FLAG_VISITED_VERDANTURF_TOWN", "VerdanturfTown",  "Verdanturf Town",  16,  4},
	{"FLAG_VISITED_PACIFIDLOG_TOWN", "PacifidlogTown",  "Pacifidlog Town",   8, 16},
	{"FLAG_VISITED_PETALBURG_CITY",  "PetalburgCity",   "Petalburg City",  20, 17},
	{"FLAG_VISITED_SLATEPORT_CITY",  "SlateportCity",   "Slateport City",  19, 20},
	{"FLAG_VISITED_MAUVILLE_CITY",   "MauvilleCity",    "Mauville City",   22,  6},
	{"FLAG_VISITED_RUSTBORO_CITY",   "RustboroCity",    "Rustboro City",   16, 39},
	{"FLAG_VISITED_FORTREE_CITY",    "FortreeCity",     "Fortree City",     5,  7},
	{"FLAG_VISITED_LILYCOVE_CITY",   "LilycoveCity",    "Lilycove City",   24, 15},
	{"FLAG_VISITED_MOSSDEEP_CITY",   "MossdeepCity",    "Mossdeep City",   28, 17},
	{"FLAG_VISITED_SOOTOPOLIS_CITY", "SootopolisCity",  "Sootopolis City", 43, 32},
	{"FLAG_VISITED_EVER_GRANDE_CITY","EverGrandeCity",  "Ever Grande City",27, 49},
};
static const int FLY_DESTINATIONS_N = sizeof(FLY_DESTINATIONS) / sizeof(FLY_DESTINATIONS[0]);

void Menu::configure(GameState* g, std::vector<Mon>* t, std::vector<Mon>* b,
                     BattleData* bd, std::vector<SavedTeam>* st) {
	this->gs = g; this->team = t; this->box = b; this->bdata = bd; this->saved_teams = st;
}

std::vector<std::pair<std::string, int>> Menu::bag_sorted() const {
	std::vector<std::pair<std::string, int>> v;
	if (this->gs)
		for (const auto& kv : this->gs->bag_items()) v.push_back(kv);
	std::sort(v.begin(), v.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	return v;
}

// Is this bag item a TM or HM? (ITEM_TM_FOCUS_PUNCH / ITEM_HM_CUT)
static bool is_machine(const std::string& item) {
	return item.rfind("ITEM_TM", 0) == 0 || item.rfind("ITEM_HM", 0) == 0;
}
static bool is_hm(const std::string& item) { return item.rfind("ITEM_HM", 0) == 0; }

// HP restored by a healing item; 0 means a full heal. -1 = not a healing item.
static int heal_amount(const std::string& item) {
	static const std::map<std::string, int> tbl = {
		{"ITEM_POTION", 20}, {"ITEM_SUPER_POTION", 50}, {"ITEM_HYPER_POTION", 200},
		{"ITEM_MAX_POTION", 0}, {"ITEM_FULL_RESTORE", 0},
		{"ITEM_FRESH_WATER", 50}, {"ITEM_SODA_POP", 60}, {"ITEM_LEMONADE", 80},
		{"ITEM_MOOMOO_MILK", 100}, {"ITEM_BERRY_JUICE", 20},
		{"ITEM_ORAN_BERRY", 10}, {"ITEM_SITRUS_BERRY", 30},
	};
	auto it = tbl.find(item);
	return it == tbl.end() ? -1 : it->second;
}
static bool is_revive_item(const std::string& item) {
	return item == "ITEM_REVIVE" || item == "ITEM_MAX_REVIVE" || item == "ITEM_REVIVAL_HERB";
}
// The single status a dedicated status healer cures; NONE if `item` isn't one.
static Status status_cured(const std::string& item) {
	if (item == "ITEM_ANTIDOTE") return Status::POISON;   // also cures TOXIC, see below
	if (item == "ITEM_PARALYZE_HEAL") return Status::PARALYSIS;
	if (item == "ITEM_AWAKENING") return Status::SLEEP;
	if (item == "ITEM_BURN_HEAL") return Status::BURN;
	if (item == "ITEM_ICE_HEAL") return Status::FREEZE;
	return Status::NONE;
}
// Full Heal cures any status; Full Restore does too, on top of a full heal.
static bool cures_any_status(const std::string& item) {
	return item == "ITEM_FULL_HEAL" || item == "ITEM_FULL_RESTORE";
}
static bool has_curable_status(const std::string& item, const Mon& m) {
	if (cures_any_status(item)) return m.status != Status::NONE;
	Status cures = status_cured(item);
	if (cures == Status::NONE) return false;
	return m.status == cures || (cures == Status::POISON && m.status == Status::TOXIC);
}

bool Menu::load_font(const std::string& path) {
	this->font_ok = this->font.loadFromFile(path);
	this->frame.load();
	this->cursor_ok = this->cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
	this->cursor_tex.setSmooth(false);
	this->region_map_ok = this->region_map_tex.loadFromFile("assets/graphics/pokenav/region_map/map.png");
	this->region_map_tex.setSmooth(false);
	this->marker_ok[0] = this->marker_tex[0].loadFromFile("assets/graphics/pokenav/region_map/brendan_icon.png");
	this->marker_ok[1] = this->marker_tex[1].loadFromFile("assets/graphics/pokenav/region_map/may_icon.png");
	this->marker_tex[0].setSmooth(false);
	this->marker_tex[1].setSmooth(false);
	this->map_sections.clear();
	std::ifstream secf("assets/pokenav/region_map_sections.tsv");
	std::string secline;
	while (std::getline(secf, secline)) {
		std::stringstream ss(secline);
		std::string id, xs, ys, ws, hs, name;
		if (!std::getline(ss, id, '\t')) continue;
		if (!std::getline(ss, xs, '\t')) continue;
		if (!std::getline(ss, ys, '\t')) continue;
		if (!std::getline(ss, ws, '\t')) continue;
		if (!std::getline(ss, hs, '\t')) continue;
		std::getline(ss, name);   // rest of the line (name may contain spaces)
		MapSecEntry e;
		e.x = std::atoi(xs.c_str()); e.y = std::atoi(ys.c_str());
		e.w = std::atoi(ws.c_str()); e.h = std::atoi(hs.c_str());
		e.name = name;
		this->map_sections.push_back(e);
	}
	return this->font_ok;
}

// Real pokeemerald's GetMapSecIdAt / region_map.c uses the same "which
// rectangle contains this grid cell" lookup; a handful of unused/FRLG-only
// entries sit at (0,0) with no real rectangle -- skip those explicitly so a
// cursor near the map's own corner doesn't wrongly report one of them.
std::string Menu::map_section_at(int grid_x, int grid_y) const {
	for (const MapSecEntry& e : this->map_sections) {
		if (e.x == 0 && e.y == 0) continue;
		if (grid_x >= e.x && grid_x < e.x + e.w &&
		    grid_y >= e.y && grid_y < e.y + e.h)
			return e.name;
	}
	return std::string();
}

static std::string pretty(const std::string& id, const std::string& prefix) {
	std::string s = id;
	if (!prefix.empty() && s.rfind(prefix, 0) == 0) s = s.substr(prefix.size());
	std::string out; bool cap = true;
	for (char c : s) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

const sf::Texture* Menu::item_icon(const std::string& item) {
	std::string f = item;
	if (f.rfind("ITEM_", 0) == 0) f = f.substr(5);
	for (char& c : f) c = (char)std::tolower((unsigned char)c);
	auto it = this->item_tex.find(f);
	if (it != this->item_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/items/" + f + ".png")) { this->item_tex[f]; return nullptr; }
	tex.setSmooth(false);
	this->item_tex[f] = tex;
	return &this->item_tex[f];
}

const sf::Texture* Menu::mon_icon(const std::string& species) {
	auto it = this->mon_tex.find(species);
	if (it != this->mon_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/pokemon/" + species + ".png")) { this->mon_tex[species]; return nullptr; }
	tex.setSmooth(false);
	this->mon_tex[species] = tex;
	return &this->mon_tex[species];
}

const sf::Texture* Menu::type_icon(const std::string& type) {
	std::string t = type;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "fighting") t = "fight";
	auto it = this->type_tex.find(t);
	if (it != this->type_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/types/" + t + ".png")) { this->type_tex[t]; return nullptr; }
	tex.setSmooth(false);
	this->type_tex[t] = tex;
	return &this->type_tex[t];
}

void Menu::open() { this->screen = MAIN; this->cursor = 0; this->flash.clear(); }
void Menu::close() { this->screen = CLOSED; }

// Teach teach_move to team[teach_cursor], consuming the TM (HMs are reusable).
void Menu::teach_selected() {
	if (!this->bdata || !this->team || this->team->empty()) return;
	if (this->teach_cursor < 0 || this->teach_cursor >= (int)this->team->size()) return;
	Mon& m = (*this->team)[this->teach_cursor];
	std::string mv_disp = pretty(this->teach_move, "");
	if (!this->bdata->can_learn_tm(m.species, this->teach_move)) {
		this->flash = pretty(m.species, "") + " kann " + mv_disp + " nicht erlernen.";
		return;
	}
	if (std::find(m.moves.begin(), m.moves.end(), this->teach_move) != m.moves.end()) {
		this->flash = pretty(m.species, "") + " kennt " + mv_disp + " bereits.";
		return;
	}
	const MoveInfo* mi = this->bdata->move(this->teach_move);
	int new_pp = mi ? mi->pp : 20;
	if (m.moves.size() < 4) { m.moves.push_back(this->teach_move); m.pp.push_back(new_pp); }
	else { m.moves[0] = this->teach_move;   // overwrite the oldest move
		if (!m.pp.empty()) m.pp[0] = new_pp; else m.pp.push_back(new_pp); }
	if (!is_hm(this->teach_item) && this->gs) this->gs->take_item(this->teach_item, 1);
	this->flash = pretty(m.species, "") + " erlernt " + mv_disp + "!";
	this->screen = BAG;
	auto items = bag_sorted();
	if (this->bag_cursor >= (int)items.size())
		this->bag_cursor = std::max(0, (int)items.size() - 1);
}

// Apply use_item to team[use_cursor]: heals HP, cures a status, and/or
// revives a fainted mon, depending on what the item actually is.
void Menu::use_selected() {
	if (!this->team || this->use_cursor < 0 || this->use_cursor >= (int)this->team->size()) return;
	Mon& m = (*this->team)[this->use_cursor];
	std::string name = pretty(m.species, "");
	const std::string& item = this->use_item;

	if (is_revive_item(item)) {
		if (!m.fainted()) { this->flash = name + " ist nicht kampfunfähig."; return; }
		m.hp = (item == "ITEM_MAX_REVIVE") ? m.max_hp : std::max(1, m.max_hp / 2);
		if (this->gs) this->gs->take_item(item, 1);
		this->flash = name + " wurde wiederbelebt!";
		this->screen = BAG;
		auto items = bag_sorted();
		if (this->bag_cursor >= (int)items.size())
			this->bag_cursor = std::max(0, (int)items.size() - 1);
		return;
	}

	bool cures = has_curable_status(item, m);
	int amt = heal_amount(item);          // -1 = this item doesn't restore HP
	bool healed = false;
	if (cures) { m.status = Status::NONE; m.status_turns = 0; }
	if (amt >= 0 && !m.fainted() && m.hp < m.max_hp) {
		m.hp = (amt == 0) ? m.max_hp : std::min(m.max_hp, m.hp + amt);
		healed = true;
	}
	if (!cures && !healed) {
		this->flash = m.fainted() ? (name + " ist kampfunfähig.")
		            : (amt >= 0)  ? (name + " hat bereits volle KP.")
		                          : (name + " hat kein Problem, das behandelt werden müsste.");
		return;
	}
	if (this->gs) this->gs->take_item(item, 1);
	this->flash = name + " wurde behandelt!";
	this->screen = BAG;
	auto items = bag_sorted();
	if (this->bag_cursor >= (int)items.size())
		this->bag_cursor = std::max(0, (int)items.size() - 1);
}

// Move team[party_cursor] into the PC box. Refuses to leave the party empty,
// same rule the real games enforce (there's always a lead mon).
void Menu::deposit_selected() {
	if (!this->team || !this->box) return;
	if (this->team->size() <= 1) { this->flash = "Du kannst dein letztes POKéMON nicht ablegen."; return; }
	if (this->party_cursor < 0 || this->party_cursor >= (int)this->team->size()) return;
	Mon m = (*this->team)[this->party_cursor];
	this->team->erase(this->team->begin() + this->party_cursor);
	this->box->push_back(m);
	if (this->party_cursor >= (int)this->team->size())
		this->party_cursor = std::max(0, (int)this->team->size() - 1);
	this->flash = pretty(m.species, "") + " wurde ins PC-Lager verschoben.";
}

// Move box[box_cursor] into the team. Refuses once the team already has 6.
void Menu::withdraw_selected() {
	if (!this->team || !this->box) return;
	if (this->box_cursor < 0 || this->box_cursor >= (int)this->box->size()) return;
	if (this->team->size() >= 6) { this->flash = "Dein Team ist bereits voll."; return; }
	Mon m = (*this->box)[this->box_cursor];
	this->box->erase(this->box->begin() + this->box_cursor);
	this->team->push_back(m);
	if (this->box_cursor >= (int)this->box->size())
		this->box_cursor = std::max(0, (int)this->box->size() - 1);
	this->flash = pretty(m.species, "") + " kam zum Team dazu.";
}

// Swap two party slots (Pokemons im Team verschieben).
void Menu::swap_party(int a, int b) {
	if (!this->team) return;
	if (a < 0 || b < 0 || a >= (int)this->team->size() || b >= (int)this->team->size()) return;
	std::swap((*this->team)[a], (*this->team)[b]);
}

// Snapshot the current team as a new named preset (mehrere gespeicherte Teams).
void Menu::save_current_team() {
	if (!this->team || !this->saved_teams || this->team->empty()) return;
	SavedTeam st;
	st.name = "TEAM " + std::to_string(this->saved_teams->size() + 1);
	st.mons = *this->team;
	this->saved_teams->push_back(st);
	this->flash = st.name + " wurde gespeichert.";
}

// Swap the active team with saved_teams[idx]'s mons: the slot keeps its name
// but now holds whatever was active before, so both teams stay reachable.
void Menu::swap_saved_team(int idx) {
	if (!this->team || !this->saved_teams) return;
	if (idx < 0 || idx >= (int)this->saved_teams->size()) return;
	SavedTeam& st = (*this->saved_teams)[idx];
	if (st.mons.empty()) { this->flash = st.name + " ist leer."; return; }
	std::vector<Mon> outgoing = *this->team;
	*this->team = st.mons;
	st.mons = outgoing;
	this->party_cursor = 0;
	this->flash = st.name + " ist jetzt dein aktives Team.";
}

void Menu::input(BtnInput b) {
	if (this->screen == MAIN) {
		if (b == BTN_UP && this->cursor > 0) this->cursor--;
		else if (b == BTN_DOWN && this->cursor < 8) this->cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->cursor == 0) { this->screen = POKEDEX; this->flash.clear(); }
			else if (this->cursor == 1) { this->screen = BAG; this->bag_cursor = 0; this->flash.clear(); }
			else if (this->cursor == 2) { this->screen = PARTY; this->party_moving = false; this->flash.clear(); }
			else if (this->cursor == 3) {
				this->screen = PC; this->pc_tab = 0; this->box_cursor = 0;
				this->teams_cursor = 0; this->flash.clear();
			}
			else if (this->cursor == 4) {
				this->screen = POKENAV;
				// Open centered on the player's own location, same as the
				// real games' region map.
				if (this->has_mapsec) {
					this->map_cur_x = this->mapsec_x + this->mapsec_w / 2;
					this->map_cur_y = this->mapsec_y + this->mapsec_h / 2;
				}
			}
			else if (this->cursor == 5) {
				// FLIEGEN: only meaningful once a party member knows FLY --
				// mirrors the badge-free "does the team know the move"
				// simplification Surf/Strength/Waterfall already use.
				bool knows_fly = false;
				if (this->team)
					for (const Mon& m : *this->team)
						if (std::find(m.moves.begin(), m.moves.end(), "FLY") != m.moves.end())
							{ knows_fly = true; break; }
				if (!knows_fly) { this->flash = "Kein POKéMON kennt FLIEGEN."; }
				else {
					this->fly_available.clear();
					for (int i = 0; i < FLY_DESTINATIONS_N; ++i)
						if (this->gs && this->gs->flag(FLY_DESTINATIONS[i].flag))
							this->fly_available.push_back(i);
					if (this->fly_available.empty())
						this->flash = "Du kennst noch keinen Zielort.";
					else { this->screen = FLY; this->fly_cursor = 0; this->flash.clear(); }
				}
			}
			else if (this->cursor == 6) { this->screen = OPTIONS; this->options_cursor = 0; }
			else if (this->cursor == 7) { this->save_requested = true; this->flash.clear(); }
			else this->screen = CLOSED;
		}
	} else if (this->screen == POKEDEX) {
		int n = this->bdata ? this->bdata->species_count() : 0;
		if (b == BTN_UP && this->dex_cursor > 0) this->dex_cursor--;
		else if (b == BTN_DOWN && this->dex_cursor + 1 < n) this->dex_cursor++;
		else if (b == BTN_LEFT || b == BTN_CONFIRM) this->screen = MAIN;
	} else if (this->screen == POKENAV) {
		// Real PokeNav's region map: moves a cursor tile by tile over the
		// 28x15 grid, naming whatever section it's currently over -- it
		// doesn't fly you anywhere itself (that's the FLY move's own
		// screen, a separate destination list below). [SPACE] exits.
		if (b == BTN_UP && this->map_cur_y > 0) this->map_cur_y--;
		else if (b == BTN_DOWN && this->map_cur_y < 14) this->map_cur_y++;
		else if (b == BTN_LEFT && this->map_cur_x > 0) this->map_cur_x--;
		else if (b == BTN_RIGHT && this->map_cur_x < 27) this->map_cur_x++;
		else if (b == BTN_CONFIRM) this->screen = MAIN;
	} else if (this->screen == FLY) {
		int n = (int)this->fly_available.size();
		if (b == BTN_UP && this->fly_cursor > 0) this->fly_cursor--;
		else if (b == BTN_DOWN && this->fly_cursor + 1 < n) this->fly_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM && this->fly_cursor < n) {
			const FlyDest& d = FLY_DESTINATIONS[this->fly_available[this->fly_cursor]];
			this->fly_map = d.map; this->fly_x = d.x; this->fly_y = d.y;
			this->fly_requested = true;
		}
	} else if (this->screen == BAG) {
		auto items = bag_sorted();
		if (b == BTN_UP && this->bag_cursor > 0) this->bag_cursor--;
		else if (b == BTN_DOWN && this->bag_cursor + 1 < (int)items.size()) this->bag_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM) {
			if (this->bag_cursor < (int)items.size()) {
				const std::string& item = items[this->bag_cursor].first;
				if (is_machine(item) && this->bdata) {
					// pokeemerald's real ITEM_TM_*/ITEM_HM_* item constants
					// are built directly from each TM/HM's move name (see
					// include/item.h's ENUM_TM/ENUM_HM, and how
					// tools/pe_import.py's parse_tm_moves() reads the same
					// FOREACH_TM/FOREACH_HM move list) -- imported scripts
					// use those literal names (`giveitem ITEM_HM_CUT`, not
					// a numbered "ITEM_HM01"), so the move is just the item
					// name with its 8-char "ITEM_TM_"/"ITEM_HM_" prefix cut.
					std::string mv = item.size() > 8 ? item.substr(8) : std::string();
					if (mv.empty() || !this->bdata->move(mv)) this->flash = "Es passiert nichts.";
					else {
						this->teach_item = item; this->teach_move = mv;
						this->teach_cursor = 0; this->flash.clear();
						this->screen = TEACH;
					}
				} else if (heal_amount(item) >= 0 || is_revive_item(item) ||
				           status_cured(item) != Status::NONE || cures_any_status(item)) {
					this->use_item = item; this->use_cursor = 0; this->flash.clear();
					this->screen = USE_ITEM;
				} else {
					this->flash = pretty(item, "ITEM_") + " kann hier nicht benutzt werden.";
				}
			}
		}
	} else if (this->screen == TEACH) {
		int n = this->team ? (int)this->team->size() : 0;
		if (b == BTN_UP && this->teach_cursor > 0) this->teach_cursor--;
		else if (b == BTN_DOWN && this->teach_cursor + 1 < n) this->teach_cursor++;
		else if (b == BTN_LEFT) this->screen = BAG;
		else if (b == BTN_CONFIRM) this->teach_selected();
	} else if (this->screen == USE_ITEM) {
		int n = this->team ? (int)this->team->size() : 0;
		if (b == BTN_UP && this->use_cursor > 0) this->use_cursor--;
		else if (b == BTN_DOWN && this->use_cursor + 1 < n) this->use_cursor++;
		else if (b == BTN_LEFT) { this->screen = BAG; this->flash.clear(); }
		else if (b == BTN_CONFIRM) this->use_selected();
	} else if (this->screen == PARTY) {
		int n = this->team ? (int)this->team->size() : 0;
		if (this->party_moving) {
			// Reorder mode (Pokemons im Team verschieben): UP/DOWN drags the
			// picked-up mon along, swapping it with whichever slot it passes.
			// CANCEL/CONFIRM both drop it back in place.
			if (b == BTN_UP && this->party_cursor > 0) {
				swap_party(this->party_cursor, this->party_cursor - 1);
				this->party_cursor--;
			} else if (b == BTN_DOWN && this->party_cursor + 1 < n) {
				swap_party(this->party_cursor, this->party_cursor + 1);
				this->party_cursor++;
			} else if (b == BTN_CANCEL || b == BTN_CONFIRM) {
				this->party_moving = false;
				this->flash.clear();
			}
		} else {
			if (b == BTN_UP && this->party_cursor > 0) this->party_cursor--;
			else if (b == BTN_DOWN && this->party_cursor + 1 < n) this->party_cursor++;
			else if (b == BTN_LEFT) this->screen = MAIN;
			else if (b == BTN_CONFIRM && this->party_cursor < n) this->screen = SUMMARY;
			else if (b == BTN_CANCEL && n > 1) {
				this->party_moving = true;
				this->flash = pretty((*this->team)[this->party_cursor].species, "") + " wird verschoben...";
			} else if (b == BTN_RIGHT && this->party_cursor < n) {
				deposit_selected();
			}
		}
	} else if (this->screen == SUMMARY) {
		if (b == BTN_LEFT || b == BTN_CONFIRM) this->screen = PARTY;
	} else if (this->screen == PC) {
		if (b == BTN_CANCEL) { this->screen = MAIN; this->flash.clear(); }
		else if (b == BTN_LEFT || b == BTN_RIGHT) {
			this->pc_tab = this->pc_tab == 0 ? 1 : 0;
			this->flash.clear();
		} else if (this->pc_tab == 0) {   // BOX tab
			int n = this->box ? (int)this->box->size() : 0;
			if (b == BTN_UP && this->box_cursor > 0) this->box_cursor--;
			else if (b == BTN_DOWN && this->box_cursor + 1 < n) this->box_cursor++;
			else if (b == BTN_CONFIRM && this->box_cursor < n) withdraw_selected();
		} else {   // TEAMS tab -- one extra row past the list to save the active team
			int n = this->saved_teams ? (int)this->saved_teams->size() : 0;
			int rows = n + 1;
			if (b == BTN_UP && this->teams_cursor > 0) this->teams_cursor--;
			else if (b == BTN_DOWN && this->teams_cursor + 1 < rows) this->teams_cursor++;
			else if (b == BTN_CONFIRM) {
				if (this->teams_cursor == n) save_current_team();
				else swap_saved_team(this->teams_cursor);
			}
		}
	} else if (this->screen == OPTIONS) {
		if (b == BTN_UP && this->options_cursor > 0) this->options_cursor--;
		else if (b == BTN_DOWN && this->options_cursor < 2) this->options_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM || b == BTN_RIGHT) {
			if (!this->gs) { /* nothing to toggle without a GameState */ }
			else if (this->options_cursor == 0) this->gs->sound_on = !this->gs->sound_on;
			else if (this->options_cursor == 1) this->gs->battle_scene_on = !this->gs->battle_scene_on;
			else this->gs->frame_type = (this->gs->frame_type + 1) % 20;
		}
	} else if (b == BTN_CONFIRM || b == BTN_LEFT) {
		this->screen = MAIN;   // back from PC
	}
}

void Menu::draw(sf::RenderTarget& target) {
	if (this->screen == CLOSED || !this->font_ok) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	// heading / body / muted text tuned for the frame's light interior
	const sf::Color head_col(24, 72, 160), body_col(40, 40, 56), muted_col(100, 100, 112);

	sf::FloatRect panel_rect(size.x * 0.45f - 20, 20, size.x * 0.55f, size.y - 40);
	this->frame.load_type(this->gs ? this->gs->frame_type : 0);   // Options: Rahmenart
	if (this->frame.ready()) {
		this->frame.draw(target, panel_rect.left, panel_rect.top,
		                 panel_rect.width, panel_rect.height, 3.f);
	} else {
		sf::RectangleShape panel(sf::Vector2f(panel_rect.width, panel_rect.height));
		panel.setPosition(panel_rect.left, panel_rect.top);
		panel.setFillColor(sf::Color(20, 28, 48, 240));
		panel.setOutlineColor(sf::Color::White); panel.setOutlineThickness(3.f);
		target.draw(panel);
	}
	sf::RectangleShape panel(sf::Vector2f(panel_rect.width, panel_rect.height));
	panel.setPosition(panel_rect.left, panel_rect.top);
	float x = panel.getPosition().x + 24, y = panel.getPosition().y + 20;

	auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color col) {
		sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), this->font, cs);
		t.setPosition(px, py); t.setFillColor(col); target.draw(t);
	};
	auto cursor_at = [&](float px, float py) {
		if (!this->cursor_ok) { text(">", px - 16, py, 20, head_col); return; }
		sf::Sprite s(this->cursor_tex);
		s.setPosition(px - 20, py + 2);
		target.draw(s);
	};
	auto hp_bar = [&](float bx, float by, int hp, int mx) {
		float w = 150, r = mx > 0 ? (float)hp / mx : 0.f;
		sf::RectangleShape bg(sf::Vector2f(w, 12)); bg.setPosition(bx, by);
		bg.setFillColor(sf::Color(190, 190, 190)); target.draw(bg);
		sf::RectangleShape fg(sf::Vector2f(w * r, 12)); fg.setPosition(bx, by);
		fg.setFillColor(r > 0.5f ? sf::Color(80, 200, 80)
		               : r > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
		target.draw(fg);
	};
	// Progress to next level, same light-blue bar as the battle HUD's.
	auto exp_bar = [&](float bx, float by, const Mon& m) {
		if (!this->bdata) return;
		float w = 150;
		std::string growth = this->bdata->growth_rate(m.species);
		long floor_now = BattleData::exp_for_level(growth, m.level);
		long floor_next = m.level < 100 ? BattleData::exp_for_level(growth, m.level + 1) : floor_now;
		float r = floor_next > floor_now
		          ? (float)(m.exp - floor_now) / (float)(floor_next - floor_now) : 1.f;
		r = std::max(0.f, std::min(1.f, r));
		sf::RectangleShape bg(sf::Vector2f(w, 5)); bg.setPosition(bx, by);
		bg.setFillColor(sf::Color(190, 190, 190)); target.draw(bg);
		sf::RectangleShape fg(sf::Vector2f(w * r, 5)); fg.setPosition(bx, by);
		fg.setFillColor(sf::Color(88, 168, 240)); target.draw(fg);
	};

	if (this->screen == MAIN) {
		text("MENÜ", x, y, 24, head_col); y += 44;
		const char* opts[] = {"POKéDEX", "BEUTEL", "POKéMON", "PC-BOX", "POKéNAV",
		                      "FLIEGEN", "OPTIONEN", "SPEICHERN", "SCHLIESSEN"};
		for (int i = 0; i < 9; ++i) {
			bool sel = i == this->cursor;
			if (sel) cursor_at(x, y + i * 38);
			text(opts[i], x, y + i * 38, 22, sel ? head_col : body_col);
		}
		if (!this->flash.empty())
			text(this->flash, x, y + 9 * 38 + 10, 16, sf::Color(30, 140, 60));
	} else if (this->screen == POKEDEX) {
		int total = this->bdata ? this->bdata->species_count() : 0;
		int seen = 0, caught = 0;
		if (this->gs)
			for (int i = 0; i < total; ++i) {
				const std::string sp = this->bdata->species_by_id(i);
				if (this->gs->is_caught(sp)) ++caught;
				else if (this->gs->is_seen(sp)) ++seen;
			}
		text("POKéDEX", x, y, 24, head_col); y += 36;
		text("Gesehen: " + std::to_string(seen + caught) +
		     "   Gefangen: " + std::to_string(caught), x, y, 16, muted_col);
		y += 34;
		const int rows = 10;
		int first = std::max(0, std::min(this->dex_cursor - rows / 2, std::max(0, total - rows)));
		for (int row = 0; row < rows && first + row < total; ++row) {
			int idx = first + row;
			std::string sp = this->bdata->species_by_id(idx);
			bool is_caught = this->gs && this->gs->is_caught(sp);
			bool is_seen = is_caught || (this->gs && this->gs->is_seen(sp));
			float ry = y + row * 34;
			bool sel = idx == this->dex_cursor;
			if (sel) cursor_at(x, ry);
			char num[16]; std::snprintf(num, sizeof(num), "#%03d", idx + 1);
			text(num, x, ry, 18, muted_col);
			if (is_seen) {
				const sf::Texture* ic = mon_icon(sp);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.5f, 0.5f); s.setPosition(x + 56, ry - 8); target.draw(s); }
				text(pretty(sp, ""), x + 96, ry, 20, is_caught ? body_col : muted_col);
				if (is_caught) text("●", x + 300, ry, 18, sf::Color(30, 140, 60));
			} else {
				text("? ? ? ? ?", x + 96, ry, 20, muted_col);
			}
		}
	} else if (this->screen == FLY) {
		text("FLIEGEN", x, y, 24, head_col); y += 44;
		if (this->fly_available.empty()) {
			text("(keine Ziele)", x, y, 20, muted_col);
		} else {
			for (int row = 0; row < (int)this->fly_available.size() && row < 10; ++row) {
				const FlyDest& d = FLY_DESTINATIONS[this->fly_available[row]];
				bool sel = row == this->fly_cursor;
				float ry = y + row * 36;
				if (sel) cursor_at(x, ry);
				text(d.name, x, ry, 20, sel ? head_col : body_col);
			}
		}
	} else if (this->screen == BAG) {
		text("BEUTEL", x, y, 24, head_col); y += 44;
		if (this->gs) {
			text("Geld: " + std::to_string(this->gs->money) + " P", x, y, 18, muted_col);
			y += 30;
		}
		auto items = bag_sorted();
		if (items.empty()) {
			text("(leer)", x, y, 20, muted_col);
		} else {
			for (int row = 0; row < (int)items.size() && row < 10; ++row) {
				const auto& kv = items[row];
				bool sel = row == this->bag_cursor;
				float ry = y + row * 40;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = item_icon(kv.first);
				if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
				bool tm = is_machine(kv.first);
				std::string label;
				if (tm) {
					// ITEM_TM_<move>/ITEM_HM_<move> -> the real games' "TM31"
					// style bag label (see move_to_tm_code()); falls back to
					// the move name itself if the code lookup ever misses.
					std::string mv = kv.first.size() > 8 ? kv.first.substr(8) : std::string();
					std::string code = this->bdata ? this->bdata->move_to_tm_code(mv) : std::string();
					label = code.empty() ? pretty(mv, "") : code;
				} else {
					label = pretty(kv.first, "ITEM_");
				}
				text(label, x + 34, ry, 20,
				     tm ? sf::Color(20, 130, 90) : body_col);
				text("x" + std::to_string(kv.second),
				     panel.getPosition().x + panel.getSize().x - 70, ry, 20, body_col);
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == TEACH) {
		text("LEHRE " + pretty(this->teach_move, ""), x, y, 22, head_col);
		y += 40;
		text("Wähle ein POKéMON:", x, y, 16, muted_col); y += 30;
		if (this->team) {
			for (int row = 0; row < (int)this->team->size() && row < 6; ++row) {
				const Mon& m = (*this->team)[row];
				bool sel = row == this->teach_cursor;
				bool able = this->bdata && this->bdata->can_learn_tm(m.species, this->teach_move);
				float ry = y + row * 44;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     able ? body_col : sf::Color(170, 170, 170));
				text(able ? "OK" : "nicht möglich", panel.getPosition().x + panel.getSize().x - 110, ry, 16,
				     able ? sf::Color(30, 150, 60) : sf::Color(190, 90, 90));
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == USE_ITEM) {
		bool revive = is_revive_item(this->use_item);
		text(pretty(this->use_item, "ITEM_") + " benutzen", x, y, 22, head_col);
		y += 40;
		text("Wähle ein POKéMON:", x, y, 16, muted_col); y += 30;
		if (this->team) {
			for (int row = 0; row < (int)this->team->size() && row < 6; ++row) {
				const Mon& m = (*this->team)[row];
				bool sel = row == this->use_cursor;
				bool able = revive ? m.fainted()
				          : has_curable_status(this->use_item, m) ||
				            (!m.fainted() && heal_amount(this->use_item) >= 0 && m.hp < m.max_hp);
				float ry = y + row * 44;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     able ? body_col : sf::Color(170, 170, 170));
				text(std::to_string(m.hp) + "/" + std::to_string(m.max_hp),
				     panel.getPosition().x + panel.getSize().x - 110, ry, 16,
				     able ? sf::Color(30, 150, 60) : sf::Color(190, 90, 90));
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == PARTY) {
		text("POKéMON", x, y, 24, head_col); y += 40;
		if (this->team) {
			int row = 0;
			for (const Mon& m : *this->team) {
				float ry = y + row * 72;
				bool sel = row == this->party_cursor;
				// While reordering, the picked-up mon's row pulses with the
				// held-item accent color instead of the plain cursor arrow,
				// so it reads as "in your hand" rather than just selected.
				if (sel && this->party_moving)
					text("↕", x - 20, ry + 6, 20, sf::Color(200, 130, 20));
				else if (sel) cursor_at(x - 12, ry + 6);
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.9f, 0.9f); s.setPosition(x, ry); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 66, ry + 6, 20, body_col);
				const sf::Texture* t1 = type_icon(m.t1);
				float tix = x + 290;
				if (t1) { sf::Sprite s(*t1); s.setScale(0.7f, 0.7f); s.setPosition(tix, ry + 2); target.draw(s); tix += 44; }
				if (m.t2 != m.t1) {
					const sf::Texture* t2 = type_icon(m.t2);
					if (t2) { sf::Sprite s(*t2); s.setScale(0.7f, 0.7f); s.setPosition(tix, ry + 2); target.draw(s); }
				}
				if (m.status != Status::NONE)
					text(BattleData::status_name(m.status), x + 290, ry + 26, 13, sf::Color(190, 60, 60));
				hp_bar(x + 66, ry + 34, m.hp, m.max_hp);
				text(std::to_string(m.hp) + "/" + std::to_string(m.max_hp), x + 226, ry + 32, 16, body_col);
				exp_bar(x + 66, ry + 52, m);
				if (++row >= 6) break;
			}
		}
		float hy = panel.getPosition().y + panel.getSize().y - 60;
		if (!this->flash.empty()) text(this->flash, x, hy, 16, sf::Color(190, 90, 20));
		else if (this->team && this->team->size() > 1)
			text(this->party_moving ? "Hoch/Runter: verschieben   Bestätigen: ablegen"
			                        : "Bestätigen: Übersicht   B: verschieben   →: ins PC",
			     x, hy, 14, muted_col);
	} else if (this->screen == SUMMARY) {
		const Mon* m = (this->team && this->party_cursor < (int)this->team->size())
			? &(*this->team)[this->party_cursor] : nullptr;
		if (!m) { text("POKéMON", x, y, 24, head_col); }
		else {
			const sf::Texture* ic = mon_icon(m->species);
			if (ic) { sf::Sprite s(*ic); s.setScale(1.3f, 1.3f); s.setPosition(x, y); target.draw(s); }
			text(pretty(m->species, "") + "  Lv" + std::to_string(m->level), x + 100, y + 10, 22, head_col);
			const sf::Texture* t1 = type_icon(m->t1);
			float tx = x + 100;
			if (t1) { sf::Sprite s(*t1); s.setPosition(tx, y + 42); target.draw(s); tx += 60; }
			if (m->t2 != m->t1) {
				const sf::Texture* t2 = type_icon(m->t2);
				if (t2) { sf::Sprite s(*t2); s.setPosition(tx, y + 42); target.draw(s); }
			}
			y += 90;
			hp_bar(x, y, m->hp, m->max_hp);
			text(std::to_string(m->hp) + "/" + std::to_string(m->max_hp), x + 160, y - 4, 18, body_col);
			y += 22;
			exp_bar(x, y, *m);
			y += 30;
			std::string ab = this->bdata ? this->bdata->ability(m->species) : "";
			text("Wesen: " + pretty(m->nature, "") + "   Fähigkeit: " +
			     (ab.empty() || ab == "NONE" ? "---" : pretty(ab, "")), x, y, 16, muted_col);
			y += 20;
			text("Hält: " + (m->held_item.empty() || m->held_item == "NONE"
			     ? std::string("---") : pretty(m->held_item, "")), x, y, 16, muted_col);
			y += 10;
			// Nature-boosted stat in a warm color, lowered in a cool one (real
			// games' own summary-screen convention), neutral otherwise.
			auto stat_col = [&](char stat) -> sf::Color {
				static const std::map<std::string, std::pair<char,char>> nat = {
					{"LONELY",{'A','D'}}, {"BRAVE",{'A','E'}}, {"ADAMANT",{'A','S'}}, {"NAUGHTY",{'A','F'}},
					{"BOLD",{'D','A'}}, {"RELAXED",{'D','E'}}, {"IMPISH",{'D','S'}}, {"LAX",{'D','F'}},
					{"TIMID",{'E','A'}}, {"HASTY",{'E','D'}}, {"JOLLY",{'E','S'}}, {"NAIVE",{'E','F'}},
					{"MODEST",{'S','A'}}, {"MILD",{'S','D'}}, {"QUIET",{'S','E'}}, {"RASH",{'S','F'}},
					{"CALM",{'F','A'}}, {"GENTLE",{'F','D'}}, {"SASSY",{'F','E'}}, {"CAREFUL",{'F','S'}},
				};
				auto it = nat.find(m->nature);
				if (it == nat.end()) return body_col;
				if (it->second.first == stat) return sf::Color(200, 60, 50);    // boosted
				if (it->second.second == stat) return sf::Color(60, 100, 200);  // lowered
				return body_col;
			};
			struct StatRow { const char* label; int val; char key; };
			StatRow rows[] = {
				{"ANGRIFF", m->atk, 'A'}, {"VERTEIDIGUNG", m->def, 'D'},
				{"SP. ANGRIFF", m->spa, 'S'}, {"SP. VERTEIDIGUNG", m->spd, 'F'},
				{"INITIATIVE", m->spe, 'E'},
			};
			for (const StatRow& r : rows) {
				text(r.label, x, y, 16, muted_col);
				text(std::to_string(r.val), x + 190, y, 16, stat_col(r.key));
				y += 24;
			}
			y += 10;
			text("Attacken:", x, y, 16, muted_col); y += 24;
			for (size_t i = 0; i < m->moves.size(); ++i) {
				const MoveInfo* mi = this->bdata ? this->bdata->move(m->moves[i]) : nullptr;
				int cur_pp = i < m->pp.size() ? m->pp[i] : (mi ? mi->pp : 0);
				int max_pp = mi ? mi->pp : cur_pp;
				text(pretty(m->moves[i], ""), x, y, 16, body_col);
				text("PP " + std::to_string(cur_pp) + "/" + std::to_string(max_pp),
				     x + 200, y, 15, cur_pp <= 0 ? sf::Color(190, 90, 90) : muted_col);
				y += 24;
			}
		}
	} else if (this->screen == OPTIONS) {
		text("OPTIONEN", x, y, 24, head_col); y += 44;
		bool sound_on = !this->gs || this->gs->sound_on;
		bool scene_on = !this->gs || this->gs->battle_scene_on;
		int ft = this->gs ? this->gs->frame_type : 0;
		struct Row { const char* label; std::string value; };
		Row rows[] = {
			{"TON", sound_on ? "AN" : "AUS"},
			{"KAMPFSZENE", scene_on ? "AN" : "AUS"},
			{"RAHMENART", std::to_string(ft + 1) + "/20"},
		};
		for (int i = 0; i < 3; ++i) {
			bool sel = i == this->options_cursor;
			if (sel) cursor_at(x, y + i * 40);
			text(rows[i].label, x, y + i * 40, 20, sel ? head_col : body_col);
			text(rows[i].value, x + 220, y + i * 40, 20, sel ? head_col : body_col);
		}
	} else if (this->screen == PC) {
		text("PC", x, y, 24, head_col);
		// Tab strip: BOX (individual storage/withdraw) vs. TEAMS (saved presets).
		const char* tabs[] = {"BOX", "TEAMS"};
		float tx = x + 90;
		for (int i = 0; i < 2; ++i) {
			bool sel = i == this->pc_tab;
			text(tabs[i], tx, y + 2, 18, sel ? head_col : muted_col);
			tx += 90;
		}
		y += 40;

		if (this->pc_tab == 0) {
			text("Aufbewahrt: " + std::to_string(this->box ? (int)this->box->size() : 0), x, y, 16, muted_col);
			y += 28;
			if (this->box && !this->box->empty()) {
				int n = (int)this->box->size();
				const int rows = 8;
				int first = std::max(0, std::min(this->box_cursor - rows / 2, std::max(0, n - rows)));
				for (int row = 0; row < rows && first + row < n; ++row) {
					int idx = first + row;
					const Mon& m = (*this->box)[idx];
					float ry = y + row * 38;
					if (idx == this->box_cursor) cursor_at(x - 12, ry + 4);
					const sf::Texture* ic = mon_icon(m.species);
					if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
					text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 44, ry, 18, body_col);
				}
			} else {
				text("(keine POKéMON aufbewahrt)", x, y, 18, muted_col);
			}
		} else {
			int n = this->saved_teams ? (int)this->saved_teams->size() : 0;
			for (int row = 0; row < n; ++row) {
				const SavedTeam& st = (*this->saved_teams)[row];
				float ry = y + row * 34;
				if (row == this->teams_cursor) cursor_at(x - 12, ry + 2);
				text(st.name, x, ry, 18, body_col);
				text(std::to_string(st.mons.size()) + " POKéMON", x + 220, ry, 15, muted_col);
			}
			// The "save current team as a new preset" row, always last.
			float sy = y + n * 34;
			if (n == this->teams_cursor) cursor_at(x - 12, sy + 2);
			text("+ Aktuelles Team speichern", x, sy, 17, n == this->teams_cursor ? head_col : sf::Color(30, 130, 90));
		}
		float hy = panel.getPosition().y + panel.getSize().y - 60;
		if (!this->flash.empty()) text(this->flash, x, hy, 16, sf::Color(190, 90, 20));
		else if (this->pc_tab == 0)
			text("Bestätigen: ins Team   ←→: Reiter   B: zurück", x, hy, 14, muted_col);
		else
			text("Bestätigen: Team wechseln/speichern   ←→: Reiter   B: zurück", x, hy, 14, muted_col);
	} else if (this->screen == POKENAV) {
		// Real PokeNav's "Hoenn Map Full View" is its own dark navy GBA
		// screen, not a page inside the light Bag/Party-style frame -- cover
		// the generic frame just drawn above with that look instead.
		const sf::Color nav_bg(16, 24, 64), nav_border(120, 180, 232);
		const sf::Color nav_head(232, 240, 255), nav_muted(160, 190, 224);
		sf::RectangleShape nav_panel(sf::Vector2f(panel_rect.width, panel_rect.height));
		nav_panel.setPosition(panel_rect.left, panel_rect.top);
		nav_panel.setFillColor(nav_bg);
		nav_panel.setOutlineColor(nav_border);
		nav_panel.setOutlineThickness(3.f);
		target.draw(nav_panel);

		text("KARTE HOENN GANZ", x, y, 20, nav_head); y += 26;
		if (!this->location.empty()) { text(this->location, x, y, 13, nav_muted); y += 20; }
		else y += 6;
		if (this->region_map_ok) {
			// Fit the map to the panel width instead of a fixed scale, so it
			// reads closer to how the original fills nearly the whole GBA
			// screen rather than sitting small inside a big white sub-panel.
			const float scale = (panel_rect.width - 48.f) / 128.f;
			const float map_w = 128.f * scale, map_h = 120.f * scale;
			sf::Sprite map_spr(this->region_map_tex);
			map_spr.setPosition(x, y);
			map_spr.setScale(scale, scale);
			target.draw(map_spr);
			float px_per_x = map_w / 28.f, px_per_y = map_h / 15.f;
			if (this->has_mapsec) {
				int gender = (this->gs && this->gs->female) ? 1 : 0;
				if (this->marker_ok[gender]) {
					float cx = (this->mapsec_x + this->mapsec_w / 2.0f) * px_per_x;
					float cy = (this->mapsec_y + this->mapsec_h / 2.0f) * px_per_y;
					sf::Sprite mk(this->marker_tex[gender]);
					mk.setScale(scale, scale);
					mk.setPosition(x + cx - 8.f * scale, y + cy - 8.f * scale);
					target.draw(mk);
				}
			}
			// The moving cursor (see Menu::input()): a bright outline over
			// whichever grid cell it's currently on, same idea as the real
			// games' blinking selection box.
			sf::RectangleShape cur_box(sf::Vector2f(px_per_x, px_per_y));
			cur_box.setPosition(x + this->map_cur_x * px_per_x, y + this->map_cur_y * px_per_y);
			cur_box.setFillColor(sf::Color(255, 240, 60, 90));
			cur_box.setOutlineColor(sf::Color(255, 240, 60, 230));
			cur_box.setOutlineThickness(-2.f);   // inward, so it stays inside the cell at this scale
			target.draw(cur_box);
			y += map_h + 10;
			// Location name box, same idea as the real screen's bottom bar
			// naming the section the cursor/marker is over.
			sf::RectangleShape loc_box(sf::Vector2f(panel_rect.width - 2.f * (x - panel_rect.left), 26));
			loc_box.setPosition(x, y);
			loc_box.setFillColor(sf::Color(8, 14, 44));
			loc_box.setOutlineColor(nav_border); loc_box.setOutlineThickness(1.f);
			target.draw(loc_box);
			std::string secname = map_section_at(this->map_cur_x, this->map_cur_y);
			text(secname.empty() ? "OFFENES MEER" : secname, x + 8, y + 3, 18, nav_head);
			y += 36;
		}
		{
			static const char* names[8] = {"STEIN", "FAUST", "DYNAMO", "HITZE",
			                               "BALANCE", "FEDER", "GEIST", "REGEN"};
			int got = 0;
			// Spread over the panel's own width (not a fixed pixel step) so
			// all 8 names fit regardless of window size, instead of running
			// off the right edge.
			float slot = (panel_rect.width - 2.f * (x - panel_rect.left)) / 8.f;
			float bx = x;
			for (int i = 0; i < 8; ++i) {
				bool have = this->gs && this->gs->flag(
					"FLAG_BADGE0" + std::to_string(i + 1) + "_GET");
				if (have) ++got;
				text(names[i], bx, y, 9,
				     have ? sf::Color(255, 210, 90) : nav_muted);
				bx += slot;
			}
			y += 20;
			text("Orden: " + std::to_string(got) + "/8", x, y, 14, nav_muted);
		}
	}
	if (this->screen == BAG)
		text("[SPACE] benutzen   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == TEACH)
		text("[SPACE] lehren   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == USE_ITEM)
		text("[SPACE] benutzen   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == OPTIONS)
		text("[SPACE]/[D] ändern   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == POKENAV)
		text("Pfeiltasten: Karte erkunden   [SPACE] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 16, sf::Color(160, 190, 224));
	else if (this->screen != MAIN)
		text("[SPACE] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	target.setView(saved);
}
