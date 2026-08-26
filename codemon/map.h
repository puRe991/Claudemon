#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <functional>
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
	// pokeemerald's LOCALID_* constant for this object event, if any script
	// addresses it by name (`applymovement`/`addobject`/`removeobject`/...)
	// -- most NPCs don't have one. See main.cpp's Session::localid_map.
	std::string local_id;
	// Trainer sight range in tiles (pokeemerald's trainer_sight_or_berry_tree_id
	// on a TRAINER_TYPE_* object event). 0 = not a trainer / never challenges
	// on sight. A trainer spots the player along its own facing direction only,
	// up to this many tiles, with nothing solid in between.
	int sight = 0;
};

class Map;   // for trainer_can_see below

// Line-of-sight test for a trainer challenging the player on sight
// (pokeemerald's GetTrainerApproachDistance, src/trainer_see.c): the player
// must be straight along `facing`, between 1 and `range` tiles away, with
// every tile in between passable and free of other actors. `blocked` is asked
// about each intervening tile so the caller can report NPCs standing in the
// way without this needing to know about Character at all.
//
// Real TRAINER_TYPE_SEE_ALL_DIRECTIONS trainers also watch sideways; this
// models the ordinary single-direction case, which covers nearly every
// trainer in the game.
bool trainer_can_see(const Map& map, int tx, int ty, DIR facing, int range,
                     int px, int py,
                     const std::function<bool(int, int)>& blocked);

// One warp/transition as authored in the map file's `warps` section. Stepping
// onto (x,y) sends the player to `dest` map, arriving at that map's warp
// number `dest_warp`.
struct Warp {
	int x, y;               // trigger tile on this map
	std::string dest;       // destination map file basename ("-" if none)
	int dest_warp;          // warp index within the destination map
};

// A seamless map edge (pokeemerald's MapConnection): walking off this map's
// `dir` edge continues into `dest`, landing on its opposite edge shifted by
// `offset` tiles (see Map::connection_for for the exact placement math).
struct Connection {
	DIR dir;
	int offset;
	std::string dest;
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
	std::string map_name;        // bare stem, e.g. "OldaleTown_PokemonCenter_1F"
	std::string visit_flag_;     // pokeemerald's FLAG_VISITED_* this map sets
	                             // unconditionally on entry (Fly destination
	                             // gating); empty if this map doesn't have one
	std::string music_;          // this map's own background music (pokeemerald's
	                             // MUS_* id, e.g. "MUS_LITTLEROOT"); empty if
	                             // the map is silent (MUS_NONE) or wasn't tagged

	std::vector<int>  tile_map;  // width*height metatile ids
	std::vector<char> solid;     // width*height passability (1 = blocked)
	std::vector<NpcSpawn> npc_spawns;
	std::vector<Warp> warp_list;
	std::vector<Connection> connection_list;
	std::vector<Sign> sign_list;
	std::unordered_set<int> grass_ids;        // metatile ids that are tall grass
	std::unordered_set<int> counter_ids;      // metatile ids that are shop/PC counters
	std::unordered_set<int> water_ids;        // metatile ids that are surfable water
	std::unordered_set<int> waterfall_ids;    // metatile ids that are MB_WATERFALL
	std::vector<EncSlot> land_slots;          // land wild-encounter table
	std::vector<EncSlot> water_slots;         // surf wild-encounter table

	std::map<std::string, std::vector<Instr>> script_defs;   // label -> instructions
	std::map<std::string, std::vector<std::string>> move_defs; // label -> actions
	std::map<std::string, std::vector<std::string>> shop_defs; // label -> ITEM_* ids
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
	// Bare map name (no "maps/" prefix, no ".map" extension) -- the same
	// form used everywhere a map is referenced by name (warps, saves, ...).
	const std::string& name() const { return map_name; }
	bool ready() const;
	// FLAG_VISITED_* this map sets unconditionally on entry, or "" if none
	// (see main.cpp's load_session -- feeds the FLIEGEN destination list).
	const std::string& visit_flag() const { return this->visit_flag_; }
	// This map's own MUS_* background music id, or "" if it's silent/
	// untagged (see Audio::play_bgm, called on every map load in main.cpp).
	const std::string& music() const { return this->music_; }

	// NPCs authored for this map.
	const std::vector<NpcSpawn>& npcs() const;

	// Warps authored for this map.
	const std::vector<Warp>& warps() const;
	const Warp* warp_at(int tile_x, int tile_y) const;   // warp on this tile, or null
	const Warp* warp_by_index(int idx) const;            // for arrival lookup

	// Seamless map edge in `dir`, or null if this map's edge in that
	// direction is just a wall (no connection authored there).
	const Connection* connection_for(DIR dir) const;

	// Signs authored for this map.
	const Sign* sign_at(int tile_x, int tile_y) const;   // sign on this tile, or null

	// Wild-encounter tall grass.
	bool is_grass(int tile_x, int tile_y) const;         // is this a grass tile?
	// Shop/PC-counter tile: impassable, but interact() should look one
	// tile past it for the clerk/nurse standing behind (pokeemerald's
	// MB_COUNTER -- there's no way to stand adjacent to them otherwise).
	bool is_counter(int tile_x, int tile_y) const;
	// Surfable water: collision-passable already (see `solid`), but only
	// enterable with Surf -- the real games gate it via the player avatar's
	// own movement check, not raw tile collision (see main.cpp's Surf gate).
	bool is_water(int tile_x, int tile_y) const;
	// MB_WATERFALL: surfable like water, but climbing it (moving north into
	// one) also needs the Waterfall HM (see main.cpp's Waterfall gate).
	bool is_waterfall(int tile_x, int tile_y) const;
	// Does stepping on (x,y) trigger a wild encounter? (tall grass, the
	// floor of a cave/indoor map that has encounters but no grass tiles, or
	// surfable water -- reaching a water tile at all already implies
	// surfing, see main.cpp's Surf gate)
	bool encounter_here(int tile_x, int tile_y) const;
	bool has_encounters() const;
	// Pick a wild pokemon by the land (or, on a water tile, surf) slot
	// weights; fills species + level.
	bool roll_encounter(std::mt19937& rng, int tile_x, int tile_y,
	                    std::string& species, int& level) const;

	// Event scripts.
	bool has_script(const std::string& label) const;
	const std::vector<Instr>& script(const std::string& label) const;
	const std::vector<std::string>& movement(const std::string& label) const;
	// `pokemart <label>`'s item list, or null if the label isn't a shop.
	const std::vector<std::string>* shop(const std::string& label) const;
	std::string npc_script(int npc_index) const;         // "" if none
	const ScriptTrigger* trigger_at(int tile_x, int tile_y) const;
	const std::vector<LoadTrigger>& on_load_triggers() const { return this->load_triggers; }

	// Runtime metatile edit (setmetatile). Returns false if out of bounds.
	bool set_metatile(int tile_x, int tile_y, int id, bool impassable);
	// Raw metatile id at (tile_x, tile_y), or -1 if out of bounds. Same
	// numbering as pokeemerald's own metatile ids (imported verbatim), so
	// engine code can recognize specific metatiles (e.g. the Mossdeep Gym
	// rotating-tile puzzle's colored arrow tiles) the way the original does.
	int metatile_at(int tile_x, int tile_y) const;
};
