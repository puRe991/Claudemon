#pragma once
#include <string>
#include <vector>
#include "GameState.h"
#include "BattleData.h"

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
}
