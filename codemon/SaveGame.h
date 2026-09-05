#pragma once
#include <string>
#include <vector>
#include "GameState.h"
#include "BattleData.h"
#include "PartySystem.h"

/******************************************************************************
SaveGame - persists the whole run (flags/vars, bag, money, party, PC box,
current map + player tile) to a single human-readable text file next to the
game binary, and restores it on the next launch.
*****************************************************************************/
namespace SaveGame {
	bool exists(const std::string& path = "savegame.dat");

	bool save(const std::string& path, const GameState& gs,
	          const std::vector<Mon>& team, const std::vector<Mon>& box,
	          const std::string& map_path, int player_x, int player_y);

	// On success, overwrites gs/team/box/map_path/player_x/player_y with the
	// saved state and returns true. Leaves everything untouched (and returns
	// false) if the file is missing or malformed.
	bool load(const std::string& path, GameState& gs,
	          std::vector<Mon>& team, std::vector<Mon>& box,
	          std::string& map_path, int& player_x, int& player_y);

	// PartySystem-shaped overloads -- the ones the game actually uses. They
	// carry two things the raw-vector pair cannot: the uid counter (so a mon
	// created after a load can never collide with a saved one) and which slot
	// is the lead / the walking companion. Loading goes through
	// PartySystem::reset(), so the party screen sees one PartyChanged rather
	// than a half-restored team.
	bool save(const std::string& path, const GameState& gs, const PartySystem& party,
	          const std::string& map_path, int player_x, int player_y);
	bool load(const std::string& path, GameState& gs, PartySystem& party,
	          std::string& map_path, int& player_x, int& player_y);

	// The party a facility is holding for the player while they battle with
	// borrowed Pokemon (the Battle Tent). It is not part of the run's state
	// the way the team and the box are -- it exists only mid-challenge -- so
	// it gets its own section, appended after a save and read back after a
	// load. Both are no-ops for an empty list / a save without the section.
	bool save_stored_party(const std::string& path, const std::vector<Mon>& mons);
	bool load_stored_party(const std::string& path, std::vector<Mon>& mons);
}
