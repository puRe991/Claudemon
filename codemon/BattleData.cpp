#include "BattleData.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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
		if (c.size() >= 11) { s.growth = c[9]; s.exp_yield = std::stoi(c[10]); }
		if (c.size() >= 12) s.catch_rate = std::stoi(c[11]);
		if (c.size() >= 14) { s.ability1 = c[12]; s.ability2 = c[13]; }
		species[c[0]] = s;
		species_index[c[0]] = (int)species_order.size();
		species_order.push_back(c[0]);
	}
	std::ifstream mv(dir + "/moves.tsv");
	while (std::getline(mv, line)) {
		auto c = split(line, '\t');
		if (c.size() < 4) continue;
		MoveInfo mi{std::stoi(c[1]), c[2], std::stoi(c[3])};
		if (c.size() >= 6) { mi.effect = c[4]; mi.secondary_chance = std::stoi(c[5]); }
		moves[c[0]] = mi;
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
	std::ifstream tp(dir + "/trainer_pics.tsv");
	while (std::getline(tp, line)) {
		auto c = split(line, '\t');
		if (c.size() >= 2) trainer_pics[c[0]] = c[1];
	}
	std::ifstream ev(dir + "/evolutions.tsv");
	while (std::getline(ev, line)) {
		auto c = split(line, '\t');
		if (c.size() < 2) continue;
		std::vector<Evolution> lst;
		for (auto& e : split(c[1], ',')) {
			auto p = split(e, ':');
			if (p.size() == 3) lst.push_back(Evolution{p[0], p[1], p[2]});
		}
		evos[c[0]] = lst;
	}
	std::ifstream tl(dir + "/tm_learnsets.tsv");
	while (std::getline(tl, line)) {
		auto c = split(line, '\t');
		if (c.size() < 2) continue;
		for (auto& mv : split(c[1], ',')) tm_learn[c[0]][mv] = true;
	}
	std::ifstream tmf(dir + "/tm_moves.tsv");
	while (std::getline(tmf, line)) {
		auto c = split(line, '\t');
		if (c.size() >= 2) tm_move[c[0]] = c[1];
	}
	loaded = !species.empty();
	return loaded;
}

const MoveInfo* BattleData::move(const std::string& m) const {
	auto it = moves.find(m);
	return it == moves.end() ? nullptr : &it->second;
}

int BattleData::species_id(const std::string& name) const {
	auto it = species_index.find(name);
	return it == species_index.end() ? -1 : it->second;
}

std::string BattleData::species_by_id(int id) const {
	return (id >= 0 && id < (int)species_order.size()) ? species_order[id] : std::string();
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
	mon.exp = exp_for_level(s.growth, mon.level);

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

std::string BattleData::trainer_pic(const std::string& t) const {
	auto it = trainer_pics.find(t);
	return it == trainer_pics.end() ? std::string() : it->second;
}

bool BattleData::is_physical(const std::string& type) {
	// Gen-3 damage split is by type.
	static const char* phys[] = {"NORMAL", "FIGHTING", "FLYING", "POISON",
		"GROUND", "ROCK", "BUG", "GHOST", "STEEL"};
	for (const char* p : phys) if (type == p) return true;
	return false;
}

Status BattleData::effect_status(const std::string& effect) {
	// Pure-status moves (Thunder Wave, Toxic, Sleep Powder, ...) and the
	// damaging moves with a chance of a secondary status (Body Slam,
	// Ice Beam, ...) share the same EFFECT_* naming for each status.
	if (effect == "SLEEP") return Status::SLEEP;
	if (effect == "POISON" || effect == "POISON_HIT") return Status::POISON;
	if (effect == "TOXIC") return Status::TOXIC;
	if (effect == "WILL_O_WISP" || effect == "BURN_HIT") return Status::BURN;
	if (effect == "PARALYZE" || effect == "PARALYZE_HIT") return Status::PARALYSIS;
	if (effect == "FREEZE_HIT") return Status::FREEZE;
	return Status::NONE;
}

bool BattleData::effect_confuses(const std::string& effect) {
	return effect == "CONFUSE" || effect == "CONFUSE_HIT";
}

const char* BattleData::status_name(Status s) {
	switch (s) {
		case Status::SLEEP:     return "SLP";
		case Status::POISON:    return "PSN";
		case Status::TOXIC:     return "PSN";
		case Status::BURN:      return "BRN";
		case Status::PARALYSIS: return "PAR";
		case Status::FREEZE:    return "FRZ";
		default:                return "";
	}
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
                       const std::string& move_name, std::mt19937& rng,
                       float atk_mult, float def_mult, bool crit) const {
	const MoveInfo* mi = move(move_name);
	if (!mi || mi->power <= 0) return 0;
	int A = (int)((is_physical(mi->type) ? atk.atk : atk.spa) * atk_mult);
	int D = (int)((is_physical(mi->type) ? def.def : def.spd) * def_mult);
	if (A <= 0) A = 1;
	if (D <= 0) D = 1;
	int base = (((2 * atk.level) / 5 + 2) * mi->power * A / D) / 50 + 2;
	float stab = (mi->type == atk.t1 || mi->type == atk.t2) ? 1.5f : 1.0f;
	float eff = type_eff(mi->type, def.t1, def.t2);
	float roll = 0.85f + (rng() % 16) / 100.0f;      // 0.85 .. 1.00
	// Burn halves physical damage output (Gen-3: applied to the attack
	// stat, equivalent to halving the final physical hit).
	float burn = (atk.status == Status::BURN && is_physical(mi->type)) ? 0.5f : 1.0f;
	int dmg = (int)(base * stab * eff * roll * burn * (crit ? 2.f : 1.f));
	return std::max(eff > 0.f ? 1 : 0, dmg);
}

// --------------------------------------------------------------- progression --
long BattleData::exp_for_level(const std::string& g, int n) {
	if (n <= 1) return 0;
	double L = n;
	if (g == "FAST")          return (long)(0.8 * L*L*L);
	if (g == "SLOW")          return (long)(1.25 * L*L*L);
	if (g == "MEDIUM_SLOW")   return (long)(1.2*L*L*L - 15*L*L + 100*L - 140);
	if (g == "ERRATIC") {
		if (n < 50)  return (long)(L*L*L * (100 - L) / 50);
		if (n < 68)  return (long)(L*L*L * (150 - L) / 100);
		if (n < 98)  return (long)(L*L*L * ((1911 - 10*n)/3) / 500);
		return (long)(L*L*L * (160 - L) / 100);
	}
	if (g == "FLUCTUATING") {
		if (n < 15)  return (long)(L*L*L * ((L+1)/3 + 24) / 50);
		if (n < 36)  return (long)(L*L*L * (L + 14) / 50);
		return (long)(L*L*L * (L/2 + 32) / 50);
	}
	return (long)(L*L*L);                     // MEDIUM_FAST
}

int BattleData::exp_yield(const std::string& s) const {
	auto it = species.find(s);
	return it == species.end() ? 50 : it->second.exp_yield;
}

void BattleData::recompute_stats(Mon& mon, bool keep_ratio) const {
	auto it = species.find(mon.species);
	if (it == species.end()) return;
	const SpeciesInfo& s = it->second;
	auto stat = [&](int base) { return (2 * base * mon.level) / 100 + 5; };
	int new_max = (2 * s.hp * mon.level) / 100 + mon.level + 10;
	if (keep_ratio) {
		float r = mon.max_hp > 0 ? (float)mon.hp / mon.max_hp : 1.f;
		mon.hp = std::max(1, (int)(new_max * r));
	} else {
		mon.hp += (new_max - mon.max_hp);      // level-up: gain the delta
	}
	mon.max_hp = new_max;
	mon.atk = stat(s.atk); mon.def = stat(s.def);
	mon.spa = stat(s.spa); mon.spd = stat(s.spd); mon.spe = stat(s.spe);
	mon.t1 = s.t1; mon.t2 = s.t2;
}

static std::string disp(const std::string& id) {
	std::string o; bool cap = true;
	for (char c : id) { if (c == '_') { o += ' '; cap = true; }
		else if (cap) { o += (char)toupper((unsigned char)c); cap = false; }
		else o += (char)tolower((unsigned char)c); }
	return o;
}

std::string BattleData::growth_rate(const std::string& species_name) const {
	auto sp = species.find(species_name);
	return sp == species.end() ? "MEDIUM_FAST" : sp->second.growth;
}

int BattleData::catch_rate(const std::string& species_name) const {
	auto sp = species.find(species_name);
	return sp == species.end() ? 45 : sp->second.catch_rate;
}

std::string BattleData::ability(const std::string& species_name) const {
	auto sp = species.find(species_name);
	return sp == species.end() ? "NONE" : sp->second.ability1;
}

void BattleData::grant_exp(Mon& mon, long gained, std::vector<std::string>& msgs) const {
	if (mon.fainted() || gained <= 0) return;
	auto sp = species.find(mon.species);
	std::string growth = sp == species.end() ? "MEDIUM_FAST" : sp->second.growth;
	mon.exp += gained;
	msgs.push_back(disp(mon.species) + " erhält " + std::to_string(gained) + " EP!");
	while (mon.level < 100 && mon.exp >= exp_for_level(growth, mon.level + 1)) {
		mon.level++;
		recompute_stats(mon, false);
		msgs.push_back(disp(mon.species) + " erreicht Level " + std::to_string(mon.level) + "!");
		// learn any move taught at this level
		auto li = learn.find(mon.species);
		if (li != learn.end()) {
			for (auto& lv : li->second) if (lv.first == mon.level) {
				if (mon.moves.size() < 4) {
					mon.moves.push_back(lv.second);
					msgs.push_back(disp(mon.species) + " erlernt " + disp(lv.second) + "!");
				} else {
					msgs.push_back(disp(mon.species) + " erlernt " + disp(lv.second) +
					               " (vergisst " + disp(mon.moves[0]) + ")");
					mon.moves[0] = lv.second;
				}
			}
		}
		// level-up evolution
		auto ei = evos.find(mon.species);
		if (ei != evos.end()) {
			for (const Evolution& e : ei->second) {
				if (e.method.rfind("LEVEL", 0) == 0 &&
				    mon.level >= std::atoi(e.param.c_str())) {
					std::string from = disp(mon.species);
					mon.species = e.target;
					recompute_stats(mon, true);
					msgs.push_back(from + " entwickelt sich zu " + disp(mon.species) + "!");
					break;
				}
			}
		}
	}
}

std::string BattleData::move_to_tm_code(const std::string& move) const {
	for (const auto& kv : tm_move) if (kv.second == move) return kv.first;
	return std::string();
}

bool BattleData::can_learn_tm(const std::string& sp, const std::string& mv) const {
	auto it = tm_learn.find(sp);
	if (it == tm_learn.end()) return false;
	return it->second.count(mv) > 0;
}
