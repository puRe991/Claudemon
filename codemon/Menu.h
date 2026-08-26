#pragma once
#include <string>
#include <map>
#include <vector>
#include "SFML/Graphics.hpp"
#include "GameState.h"
#include "BattleData.h"   // Mon
#include "Battle.h"       // BtnInput
#include "UiFrame.h"

/******************************************************************************
Menu - the overworld start menu with a Bag (item icons + counts) and a Pokemon
(party) screen. Opened with M; drawn in screen space over the world.
*****************************************************************************/
class Menu
{
private:
	enum Screen { CLOSED, MAIN, BAG, PARTY, PC, POKENAV, TEACH, USE_ITEM, FLY, POKEDEX, SUMMARY, OPTIONS };
	sf::Font font; bool font_ok;
	Screen screen;
	int cursor;
	int bag_cursor;         // selected row in the BAG list
	int teach_cursor;       // selected party member when teaching a TM/HM
	std::string teach_item; // ITEM_TMxx / ITEM_HMxx being taught
	std::string teach_move; // the move that TM/HM teaches
	int use_cursor;         // selected party member for a healing/revive item
	std::string use_item;   // ITEM_POTION / ITEM_REVIVE / ... pending use
	int fly_cursor;         // selected destination in the FLY screen
	int dex_cursor = 0;     // selected species (absolute index) in the POKEDEX screen
	int party_cursor = 0;   // selected party member in the PARTY/SUMMARY screens
	int options_cursor = 0; // selected row (Ton/Kampfszene/Rahmenart) in the OPTIONS screen
	// Visited-town destinations available right now (filtered from the fixed
	// FLY_DESTINATIONS table by GameState::flag() each time FLY opens).
	std::vector<int> fly_available;
	bool fly_requested = false;
	std::string fly_map;    // pending fly destination once confirmed
	int fly_x = 0, fly_y = 0;
	std::string flash;      // transient status line (e.g. "X learned MOVE!")
	GameState* gs;
	BattleData* bdata;
	std::vector<Mon>* team;
	std::vector<Mon>* box;
	std::string location;

	// Sorted snapshot of the bag (unordered_map has no stable order for a cursor).
	std::vector<std::pair<std::string, int>> bag_sorted() const;
	// Teach the pending TM/HM move to the selected party member.
	void teach_selected();
	// Apply the pending healing/revive item to the selected party member.
	void use_selected();
	std::map<std::string, sf::Texture> item_tex;
	std::map<std::string, sf::Texture> mon_tex;
	std::map<std::string, sf::Texture> type_tex;

	const sf::Texture* item_icon(const std::string& item);
	const sf::Texture* mon_icon(const std::string& species);
	const sf::Texture* type_icon(const std::string& type);
	UiFrame frame;
	sf::Texture cursor_tex; bool cursor_ok;

	// PokeNav Town Map (drawn inline in the POKENAV screen): the real Hoenn
	// map image plus the player's gendered marker icon (pokeemerald's own
	// brendan_icon.png/may_icon.png).
	sf::Texture region_map_tex; bool region_map_ok = false;
	sf::Texture marker_tex[2]; bool marker_ok[2] = {false, false};   // 0=Brendan, 1=May
	// Current map's region-map grid rectangle, set by main.cpp's set_mapsec()
	// on every map load/warp (mirrors set_location() below). has_mapsec false
	// means this map isn't on the region map at all (Battle Frontier
	// interiors, ...), so no marker is drawn.
	bool has_mapsec = false;
	int mapsec_x = 0, mapsec_y = 0, mapsec_w = 1, mapsec_h = 1;

	// The full region-map section table (all ~213 MAPSEC_* entries, real
	// pokeemerald region_map_sections.json via pe_import.py), for the
	// POKENAV map screen's moving cursor to name whatever grid cell it's
	// currently over -- not just the player's own location above.
	struct MapSecEntry { int x, y, w, h; std::string name; };
	std::vector<MapSecEntry> map_sections;
	std::string map_section_at(int grid_x, int grid_y) const;
	// Cursor position on that 28x15 grid; reset to the player's own location
	// each time POKENAV opens (see Menu::input()'s MAIN handler).
	int map_cur_x = 14, map_cur_y = 7;

public:
	Menu();
	void configure(GameState* g, std::vector<Mon>* team, std::vector<Mon>* box,
	               BattleData* bd = nullptr);
	void set_location(const std::string& loc) { this->location = loc; }
	void set_mapsec(bool has, int x, int y, int w, int h) {
		this->has_mapsec = has;
		this->mapsec_x = x; this->mapsec_y = y;
		this->mapsec_w = w > 0 ? w : 1; this->mapsec_h = h > 0 ? h : 1;
	}
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	bool active() const { return screen != CLOSED; }
	void open();
	void close();
	void input(BtnInput b);
	void draw(sf::RenderTarget& target);

	// SPEICHERN in the main menu: the menu can't touch the save file itself
	// (it doesn't know the current map/player position), so it just raises a
	// request flag; the game loop performs the actual save and reports the
	// result back via set_flash().
	bool wants_save() const { return this->save_requested; }
	void ack_save() { this->save_requested = false; }
	void set_flash(const std::string& s) { this->flash = s; }

	// FLIEGEN in the main menu: same request/ack shape as SPEICHERN, but with
	// a destination map+tile the game loop resolves via load_session (the
	// menu itself can't touch the session, same reasoning as save above).
	bool wants_fly() const { return this->fly_requested; }
	void fly_destination(std::string& map, int& x, int& y) const {
		map = this->fly_map; x = this->fly_x; y = this->fly_y;
	}
	void ack_fly() { this->fly_requested = false; this->screen = CLOSED; }

private:
	bool save_requested = false;
};
