#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <random>

/******************************************************************************
BattleData - loads the imported battle tables (species base stats, moves,
learnsets, trainer parties) and builds fightable pokemon from them, plus the
Gen-3 type chart and damage formula.
*****************************************************************************/
struct SpeciesInfo {
	int hp, atk, def, spa, spd, spe;
	std::string t1, t2;
};

struct MoveInfo {
	int power;
	std::string type;
	int accuracy;
};

// A battle-ready pokemon with computed stats and up to four moves.
struct Mon {
	std::string species;
	int level = 5;
	int max_hp = 1, hp = 1;
	int atk = 1, def = 1, spa = 1, spd = 1, spe = 1;
	std::string t1 = "NORMAL", t2 = "NORMAL";
	std::vector<std::string> moves;
	bool fainted() const { return hp <= 0; }
};

class BattleData
{
private:
	std::unordered_map<std::string, SpeciesInfo> species;
	std::unordered_map<std::string, MoveInfo> moves;
	std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> learn;
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> trainers;
	bool loaded = false;

public:
	bool load(const std::string& dir = "assets/battle");
	bool is_loaded() const { return loaded; }

	bool has_species(const std::string& s) const { return species.count(s) > 0; }
	const MoveInfo* move(const std::string& m) const;

	// Build a level-scaled pokemon with its natural (level-up) moveset.
	Mon make_mon(const std::string& species_name, int level) const;

	// Trainer's party, or empty if unknown.
	std::vector<std::pair<std::string, int>> trainer_party(const std::string& t) const;

	// Type effectiveness multiplier of an attacking type vs a defender's types.
	static float type_eff(const std::string& atk,
	                      const std::string& d1, const std::string& d2);
	// Is a move type physical (uses Atk/Def) in the Gen-3 split?
	static bool is_physical(const std::string& type);

	// Damage of `attacker` using `move_name` against `defender`.
	int damage(const Mon& attacker, const Mon& defender,
	           const std::string& move_name, std::mt19937& rng) const;
};
