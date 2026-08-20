#include "BattleData.h"
#include <fstream>
#include <sstream>
#include <algorithm>

static std::vector<std::string> split(const std::string& s, char d) {
	std::vector<std::string> out; std::stringstream ss(s); std::string t;
	while (std::getline(ss, t, d)) out.push_back(t);
	return out;
}

bool BattleData::load(const std::string& dir) {
	std::ifstream sp(dir + "/species.tsv");
	if (!sp.is_open()) return false;
	std::string line;
	while (std::getline(sp, line)) {
		auto c = split(line, '\t');
		if (c.size() < 9) continue;
		SpeciesInfo s;
		s.hp = std::stoi(c[1]); s.atk = std::stoi(c[2]); s.def = std::stoi(c[3]);
		s.spa = std::stoi(c[4]); s.spd = std::stoi(c[5]); s.spe = std::stoi(c[6]);
		s.t1 = c[7]; s.t2 = c[8];
		species[c[0]] = s;
	}
	std::ifstream mv(dir + "/moves.tsv");
	while (std::getline(mv, line)) {
		auto c = split(line, '\t');
		if (c.size() < 4) continue;
		moves[c[0]] = MoveInfo{std::stoi(c[1]), c[2], std::stoi(c[3])};
	}
	std::ifstream ls(dir + "/learnsets.tsv");
	while (std::getline(ls, line)) {
		auto c = split(line, '\t');
		if (c.size() < 2) continue;
		std::vector<std::pair<int, std::string>> lst;
		for (auto& e : split(c[1], ',')) {
			auto kv = split(e, ':');
			if (kv.size() == 2) lst.push_back({std::stoi(kv[0]), kv[1]});
		}
		learn[c[0]] = lst;
	}
	std::ifstream tr(dir + "/trainers.tsv");
	while (std::getline(tr, line)) {
		auto c = split(line, '\t');
		if (c.size() < 2) continue;
		std::vector<std::pair<std::string, int>> party;
		for (auto& e : split(c[1], ',')) {
			auto kv = split(e, ':');
			if (kv.size() == 2) party.push_back({kv[0], std::stoi(kv[1])});
		}
		trainers[c[0]] = party;
	}
	loaded = !species.empty();
	return loaded;
}

const MoveInfo* BattleData::move(const std::string& m) const {
	auto it = moves.find(m);
	return it == moves.end() ? nullptr : &it->second;
}

Mon BattleData::make_mon(const std::string& name, int level) const {
	Mon mon; mon.species = name; mon.level = std::max(1, level);
	auto it = species.find(name);
	if (it == species.end()) { mon.max_hp = mon.hp = 10 + level; return mon; }
	const SpeciesInfo& s = it->second;
	auto stat = [&](int base) { return (2 * base * mon.level) / 100 + 5; };
	mon.max_hp = mon.hp = (2 * s.hp * mon.level) / 100 + mon.level + 10;
	mon.atk = stat(s.atk); mon.def = stat(s.def);
	mon.spa = stat(s.spa); mon.spd = stat(s.spd); mon.spe = stat(s.spe);
	mon.t1 = s.t1; mon.t2 = s.t2;

	// natural moveset: the last (up to) 4 moves learned by this level
	auto li = learn.find(name);
	if (li != learn.end()) {
		std::vector<std::string> avail;
		for (auto& lv : li->second) if (lv.first <= mon.level) avail.push_back(lv.second);
		int start = std::max(0, (int)avail.size() - 4);
		for (int k = start; k < (int)avail.size(); ++k) mon.moves.push_back(avail[k]);
	}
	// guarantee at least one usable, damaging move
	bool has_dmg = false;
	for (auto& m : mon.moves) { const MoveInfo* mi = move(m); if (mi && mi->power > 0) has_dmg = true; }
	if (mon.moves.empty() || !has_dmg) mon.moves.push_back("TACKLE");
	return mon;
}

std::vector<std::pair<std::string, int>>
BattleData::trainer_party(const std::string& t) const {
	auto it = trainers.find(t);
	return it == trainers.end() ? std::vector<std::pair<std::string, int>>() : it->second;
}

bool BattleData::is_physical(const std::string& type) {
	// Gen-3 damage split is by type.
	static const char* phys[] = {"NORMAL", "FIGHTING", "FLYING", "POISON",
		"GROUND", "ROCK", "BUG", "GHOST", "STEEL"};
	for (const char* p : phys) if (type == p) return true;
	return false;
}

// Compact Gen-3 type chart. eff(a,d): 2 = super, 1 = normal, 0 = half, -1 = none.
static int chart(const std::string& a, const std::string& d) {
	struct E { const char* a; const char* d; int v; };
	static const E t[] = {
		{"NORMAL","ROCK",0},{"NORMAL","STEEL",0},{"NORMAL","GHOST",-1},
		{"FIRE","FIRE",0},{"FIRE","WATER",0},{"FIRE","GRASS",2},{"FIRE","ICE",2},
		{"FIRE","BUG",2},{"FIRE","ROCK",0},{"FIRE","DRAGON",0},{"FIRE","STEEL",2},
		{"WATER","FIRE",2},{"WATER","WATER",0},{"WATER","GRASS",0},{"WATER","GROUND",2},
		{"WATER","ROCK",2},{"WATER","DRAGON",0},
		{"GRASS","FIRE",0},{"GRASS","WATER",2},{"GRASS","GRASS",0},{"GRASS","POISON",0},
		{"GRASS","GROUND",2},{"GRASS","FLYING",0},{"GRASS","BUG",0},{"GRASS","ROCK",2},
		{"GRASS","DRAGON",0},{"GRASS","STEEL",0},
		{"ELECTRIC","WATER",2},{"ELECTRIC","ELECTRIC",0},{"ELECTRIC","GRASS",0},
		{"ELECTRIC","GROUND",-1},{"ELECTRIC","FLYING",2},{"ELECTRIC","DRAGON",0},
		{"ICE","FIRE",0},{"ICE","WATER",0},{"ICE","GRASS",2},{"ICE","ICE",0},
		{"ICE","GROUND",2},{"ICE","FLYING",2},{"ICE","DRAGON",2},{"ICE","STEEL",0},
		{"FIGHTING","NORMAL",2},{"FIGHTING","ICE",2},{"FIGHTING","POISON",0},
		{"FIGHTING","FLYING",0},{"FIGHTING","PSYCHIC",0},{"FIGHTING","BUG",0},
		{"FIGHTING","ROCK",2},{"FIGHTING","GHOST",-1},{"FIGHTING","DARK",2},
		{"FIGHTING","STEEL",2},
		{"POISON","GRASS",2},{"POISON","POISON",0},{"POISON","GROUND",0},
		{"POISON","ROCK",0},{"POISON","GHOST",0},{"POISON","STEEL",-1},
		{"GROUND","FIRE",2},{"GROUND","ELECTRIC",2},{"GROUND","GRASS",0},
		{"GROUND","POISON",2},{"GROUND","FLYING",-1},{"GROUND","BUG",0},
		{"GROUND","ROCK",2},{"GROUND","STEEL",2},
		{"FLYING","ELECTRIC",0},{"FLYING","GRASS",2},{"FLYING","FIGHTING",2},
		{"FLYING","BUG",2},{"FLYING","ROCK",0},{"FLYING","STEEL",0},
		{"PSYCHIC","FIGHTING",2},{"PSYCHIC","POISON",2},{"PSYCHIC","PSYCHIC",0},
		{"PSYCHIC","DARK",-1},{"PSYCHIC","STEEL",0},
		{"BUG","FIRE",0},{"BUG","GRASS",2},{"BUG","FIGHTING",0},{"BUG","POISON",0},
		{"BUG","FLYING",0},{"BUG","PSYCHIC",2},{"BUG","GHOST",0},{"BUG","DARK",2},
		{"BUG","STEEL",0},
		{"ROCK","FIRE",2},{"ROCK","ICE",2},{"ROCK","FIGHTING",0},{"ROCK","GROUND",0},
		{"ROCK","FLYING",2},{"ROCK","BUG",2},{"ROCK","STEEL",0},
		{"GHOST","NORMAL",-1},{"GHOST","PSYCHIC",2},{"GHOST","GHOST",2},{"GHOST","DARK",0},
		{"DRAGON","DRAGON",2},{"DRAGON","STEEL",0},
		{"DARK","FIGHTING",0},{"DARK","PSYCHIC",2},{"DARK","GHOST",2},{"DARK","DARK",0},
		{"STEEL","FIRE",0},{"STEEL","WATER",0},{"STEEL","ELECTRIC",0},{"STEEL","ICE",2},
		{"STEEL","ROCK",2},{"STEEL","STEEL",0},
	};
	for (const E& e : t) if (a == e.a && d == e.d) return e.v;
	return 1;
}

float BattleData::type_eff(const std::string& a, const std::string& d1,
                           const std::string& d2) {
	auto mul = [](int v) { return v == 2 ? 2.f : v == 0 ? 0.5f : v == -1 ? 0.f : 1.f; };
	float e = mul(chart(a, d1));
	if (!d2.empty() && d2 != d1) e *= mul(chart(a, d2));
	return e;
}

int BattleData::damage(const Mon& atk, const Mon& def,
                       const std::string& move_name, std::mt19937& rng) const {
	const MoveInfo* mi = move(move_name);
	if (!mi || mi->power <= 0) return 0;
	int A = is_physical(mi->type) ? atk.atk : atk.spa;
	int D = is_physical(mi->type) ? def.def : def.spd;
	if (D <= 0) D = 1;
	int base = (((2 * atk.level) / 5 + 2) * mi->power * A / D) / 50 + 2;
	float stab = (mi->type == atk.t1 || mi->type == atk.t2) ? 1.5f : 1.0f;
	float eff = type_eff(mi->type, def.t1, def.t2);
	float roll = 0.85f + (rng() % 16) / 100.0f;      // 0.85 .. 1.00
	int dmg = (int)(base * stab * eff * roll);
	return std::max(eff > 0.f ? 1 : 0, dmg);
}
