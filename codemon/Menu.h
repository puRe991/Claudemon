#pragma once
#include <string>
#include <map>
#include <vector>
#include "SFML/Graphics.hpp"
#include "GameState.h"
#include "BattleData.h"   // Mon
#include "PartySystem.h"
#include "Battle.h"       // BtnInput
#include "UiFrame.h"

/******************************************************************************
Menu - the overworld start menu with a Bag (item icons + counts) and a Pokemon
(party) screen. Opened with M; drawn in screen space over the world.

The party half of this screen owns no game data: it reads PartySystem and calls
its operations, and PartySystem's events tell it which row to redraw. See
PartySystem.h for why the data lives there instead of here.
*****************************************************************************/

// Which actions the party screen may offer right now. Field and box menus
// answer that question differently (design brief §26), so the filter is a
// parameter rather than a hardcoded list.
enum class PartyContext { FIELD, BOX };

// One row of the "what do you want to do with this POKéMON?" menu.
enum class PartyAction {
	SUMMARY,        // Bericht
	SWAP,           // Pokemon bewegen/wechseln (party order only)
	GIVE_ITEM,
	TAKE_ITEM,
	TO_BOX,
	TO_PARTY,       // from the box side
	SET_LEAD,       // make this the mon a battle starts with
	SET_COMPANION,  // walk with this one (§25)
	CANCEL,
};

class Menu
{
private:
	enum Screen { CLOSED, MAIN, BAG, PARTY, PC, POKENAV, TEACH, USE_ITEM, FLY, POKEDEX,
	              SUMMARY, OPTIONS, PARTY_ACTION, GIVE_ITEM, MOVE_LEARN };
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
	// Selected party SLOT (0..5) in the PARTY screen -- slots, not members, so
	// the cursor can sit on an empty one exactly like the real games.
	int party_cursor = 0;
	int box_cursor = 0;     // selected stored mon in the PC screen

	// --- party action menu -------------------------------------------------
	std::vector<PartyAction> actions;   // rebuilt per selection, see build_actions()
	int action_cursor = 0;
	int action_slot = 0;                // slot the action menu was opened on
	PartyContext action_context = PartyContext::FIELD;
	// While >= 0 the party screen is picking the second half of a swap: the
	// next confirmed slot changes the party order (§4/§13).
	int swap_from = -1;

	// --- summary -----------------------------------------------------------
	SummaryPage summary_page = SummaryPage::OVERVIEW;
	bool summary_from_box = false;      // the summary can be opened over a box mon
	int summary_index = 0;              // party slot, or box index when from_box

	// --- give held item ----------------------------------------------------
	int give_cursor = 0;
	int give_slot = 0;
	std::vector<std::string> give_items;   // holdable bag entries, rebuilt on open

	// --- "which move should be forgotten?" ---------------------------------
	// The prompt PartySystem queues when a level-up move does not fit (§9).
	// The menu only renders and answers it; the request itself lives in the
	// party system, so it survives the menu being closed and reopened.
	int learn_cursor = 0;               // 0..3 = replace that move, 4 = decline
	// The TM to spend once the prompt is answered with a replacement (empty
	// for a level-up move and for HMs, which are reusable). A declined TM is
	// never consumed, same as the real games.
	std::string learn_item;
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
	PartySystem* party;
	std::string location;

	// Cached per-slot presentation, rebuilt only for the slots whose
	// PartySystem revision moved (§28: a Pikachu losing 10 HP redraws that one
	// row, it does not rebuild the party screen).
	struct SlotView {
		unsigned rev = 0;
		bool present = false;
		std::string name, level_text, hp_text, status_text, item_text, gender;
		std::string item_id;   // raw ITEM_* id, for the icon lookup
		std::string sprite;
		bool shiny = false, fainted = false;
		int hp = 0, max_hp = 0;
	};
	SlotView slot_views[PartySystem::MAX_SLOTS];
	int party_token = -1;        // PartySystem subscription, -1 when not subscribed
	// Slots the party system told us to refresh since the last draw.
	bool slot_dirty[PartySystem::MAX_SLOTS] = {true, true, true, true, true, true};
	void on_party_event(const PartyNotice& n);
	void refresh_slot_views();               // rebuilds only the dirty rows
	const SlotView& slot_view(int slot) const { return this->slot_views[slot]; }

	// Which actions are valid for `slot` in `ctx` right now -- an action that
	// cannot succeed is never offered (§5/§26).
	std::vector<PartyAction> build_actions(int slot, PartyContext ctx) const;
	static const char* action_label(PartyAction a);
	void run_action(PartyAction a);
	// Report a PartyResult to the player via the status line.
	void report(PartyResult r, const std::string& ok_text = std::string());

	// Sorted snapshot of the bag (unordered_map has no stable order for a cursor).
	std::vector<std::pair<std::string, int>> bag_sorted() const;
	// Teach the pending TM/HM move to the selected party member.
	void teach_selected();
	// Apply the pending healing/revive item to the selected party member.
	void use_selected();
	std::map<std::string, sf::Texture> item_tex;
	std::map<std::string, sf::Texture> mon_tex;   // keyed by sprite path (normal vs shiny)
	std::map<std::string, sf::Texture> type_tex;

	const sf::Texture* item_icon(const std::string& item);
	// `shiny` picks this individual's own artwork; the Pokedex, which lists
	// species rather than individuals, always asks for the normal set.
	const sf::Texture* mon_icon(const std::string& species, bool shiny = false);
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
	// The menu reads the party through PartySystem and never keeps its own
	// copy of it.
	void configure(GameState* g, PartySystem* party, BattleData* bd = nullptr);
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
	// Jump straight to the party screen (a script or the field's "use an HM"
	// flow wanting the player to pick a mon).
	void open_party();
	// Open the pending "which move should be forgotten?" prompt. Returns
	// false when PartySystem has no request waiting.
	bool open_move_learn();
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
