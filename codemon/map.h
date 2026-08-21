#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <random>

#include "Tileset.h"
#include "Window.h"
#include "SFML/Graphics.hpp"
#include "direction.h"
#include "Coordinates.h"

// How an NPC behaves once placed on the map (from pokeemerald movement_type).
enum MoveKind { MOVE_STATIC = 0, MOVE_WANDER = 1, MOVE_PACE_V = 2, MOVE_PACE_H = 3 };

// One NPC as authored in the map file's `npcs` section.
struct NpcSpawn {
	std::string sheet;   // overworld sheet path key, e.g. "people_mom"
	int x, y;            // start tile
	DIR facing;          // initial facing
	MoveKind movement;   // behaviour
	std::string dialog;  // line shown on interaction (may be empty)
	std::string hide_flag; // pokeemerald FLAG_HIDE_*; set -> not spawned (empty = always shown)
};

// One warp/transition as authored in the map file's `warps` section. Stepping
// onto (x,y) sends the player to `dest` map, arriving at that map's warp
// number `dest_warp`.
struct Warp {
	int x, y;               // trigger tile on this map
	std::string dest;       // destination map file basename ("-" if none)
	int dest_warp;          // warp index within the destination map
};

// A readable sign (bg_event). `text` may contain U+001F page separators.
struct Sign {
	int x, y;
	std::string text;
};

// A coord_event: stepping on (x,y) while var==val runs `label`.
struct ScriptTrigger {
	int x, y;
	std::string var;
	std::string val;   // symbolic; resolved by the VM
	std::string label;
};

// A pokeemerald MAP_SCRIPT_ON_FRAME_TABLE entry: while var==val, run `label`
// once on entering the map (its own body updates var so it won't refire).
// Used for one-time "just arrived here" setup, e.g. Route 101 advancing
// VAR_ROUTE101_STATE from 0 to 1 -- the same condition the Birch-rescue
// coord trigger waits on.
struct LoadTrigger {
	std::string var, val, label;
};

// One decoded script instruction: opcode plus string arguments.
using Instr = std::vector<std::string>;

// One wild-encounter slot: a species and its level range (pokeemerald slot
// order encodes the encounter rate).
struct EncSlot {
	std::string species;
	int min_level, max_level;
};

/******************************************************************************
Map - a grid of metatile ids rendered through a Tileset.

Map file format (text), e.g. maps/route.map:

    tileset general        # metatile sheet: assets/tilesets/general.png
    20 15                  # width height (in metatiles)
    3 6                    # player start tile (x y)
    <height rows of width comma-separated metatile ids>
    collision              # optional marker
    <height rows of width comma-separated 0/1 passability flags>

Rendering samples 16x16 metatiles from the coloured tileset sheet, so the map
looks exactly like the source pokeemerald tiles with no runtime palette work.
*****************************************************************************/
class Map
{
private:
	Tileset tileset;             // coloured metatile sheet
	int tile_px;                 // metatile edge length (px), from the tileset
	Coordinates dimensions;      // width, height in tiles
	Coordinates start_pos;       // player spawn tile

	std::vector<int>  tile_map;  // width*height metatile ids
	std::vector<char> solid;     // width*height passability (1 = blocked)
	std::vector<NpcSpawn> npc_spawns;
	std::vector<Warp> warp_list;
	std::vector<Sign> sign_list;
	std::unordered_set<int> grass_ids;        // metatile ids that are tall grass
	std::vector<EncSlot> land_slots;          // land wild-encounter table

	std::map<std::string, std::vector<Instr>> script_defs;   // label -> instructions
	std::map<std::string, std::vector<std::string>> move_defs; // label -> actions
	std::unordered_map<int, std::string> npc_script_map;      // npc index -> label
	std::vector<ScriptTrigger> script_triggers;
	std::vector<LoadTrigger> load_triggers;

	int index(int x, int y) const;

public:
	// Load a map file; tileset_dir is where the coloured sheets live.
	Map(const std::string& map_path,
	    const std::string& tileset_dir = "assets/tilesets");

	// Render every metatile into the window.
	void render_map(Window* active_window);
	// Render every metatile into any SFML target (window or off-screen texture).
	void render_to(sf::RenderTarget& target);

	// Is (tile_x, tile_y) inside the map?
	bool in_bounds(int tile_x, int tile_y) const;
	bool in_bounds(Coordinates proposed_coord) const;

	// Can a character stand on (tile_x, tile_y)? (in bounds AND not blocked)
	bool passable(int tile_x, int tile_y) const;

	unsigned int get_width() const;
	unsigned int get_height() const;
	int get_tile_size() const;
	Coordinates get_start_pos() const;
	bool ready() const;

	// NPCs authored for this map.
	const std::vector<NpcSpawn>& npcs() const;

	// Warps authored for this map.
	const std::vector<Warp>& warps() const;
	const Warp* warp_at(int tile_x, int tile_y) const;   // warp on this tile, or null
	const Warp* warp_by_index(int idx) const;            // for arrival lookup

	// Signs authored for this map.
	const Sign* sign_at(int tile_x, int tile_y) const;   // sign on this tile, or null

	// Wild-encounter tall grass.
	bool is_grass(int tile_x, int tile_y) const;         // is this a grass tile?
	// Does stepping on (x,y) trigger a wild encounter? (tall grass, or the
	// floor of a cave/indoor map that has encounters but no grass tiles)
	bool encounter_here(int tile_x, int tile_y) const;
	bool has_encounters() const;
	// Pick a wild pokemon by the land slot weights; fills species + level.
	bool roll_encounter(std::mt19937& rng, std::string& species, int& level) const;

	// Event scripts.
	bool has_script(const std::string& label) const;
	const std::vector<Instr>& script(const std::string& label) const;
	const std::vector<std::string>& movement(const std::string& label) const;
	std::string npc_script(int npc_index) const;         // "" if none
	const ScriptTrigger* trigger_at(int tile_x, int tile_y) const;
	const std::vector<LoadTrigger>& on_load_triggers() const { return this->load_triggers; }

	// Runtime metatile edit (setmetatile). Returns false if out of bounds.
	bool set_metatile(int tile_x, int tile_y, int id, bool impassable);
};
