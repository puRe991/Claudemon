#include "BattleData.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::vector<std::string> split(const std::string& s, char d) {
	std::vector<std::string> out; std::stringstream ss(s); std::string t;
	while (std::getline(ss, t, d)) out.push_back(t);
	return out;
}

// pokeemerald's real gNatureStatTable (src/pokemon.c): -1/0/+1 per stat,
// Attack/Defense/Speed/Sp.Atk/Sp.Def order, five neutral natures included.
struct NatureRow { const char* name; int atk, def, spe, spa, spd; };
static const NatureRow NATURES[25] = {
	{"HARDY", 0, 0, 0, 0, 0}, {"LONELY", +1, -1, 0, 0, 0}, {"BRAVE", +1, 0, -1, 0, 0},
	{"ADAMANT", +1, 0, 0, -1, 0}, {"NAUGHTY", +1, 0, 0, 0, -1}, {"BOLD", -1, +1, 0, 0, 0},
	{"DOCILE", 0, 0, 0, 0, 0}, {"RELAXED", 0, +1, -1, 0, 0}, {"IMPISH", 0, +1, 0, -1, 0},
	{"LAX", 0, +1, 0, 0, -1}, {"TIMID", -1, 0, +1, 0, 0}, {"HASTY", 0, -1, +1, 0, 0},
	{"SERIOUS", 0, 0, 0, 0, 0}, {"JOLLY", 0, 0, +1, -1, 0}, {"NAIVE", 0, 0, +1, 0, -1},
	{"MODEST", -1, 0, 0, +1, 0}, {"MILD", 0, -1, 0, +1, 0}, {"QUIET", 0, 0, -1, +1, 0},
	{"BASHFUL", 0, 0, 0, 0, 0}, {"RASH", 0, 0, 0, +1, -1}, {"CALM", -1, 0, 0, 0, +1},
	{"GENTLE", 0, -1, 0, 0, +1}, {"SASSY", 0, 0, -1, 0, +1}, {"CAREFUL", 0, 0, 0, -1, +1},
	{"QUIRKY", 0, 0, 0, 0, 0},
};
static const NatureRow* nature_row(const std::string& n) {
	for (const NatureRow& r : NATURES) if (n == r.name) return &r;
	return &NATURES[0];   // Hardy (neutral) for an unrecognized name
}
// -1/0/+1 -> the real 0.9/1.0/1.1 multiplier.
static float nature_mult(int delta) { return 1.f + delta * 0.1f; }
// Gen-3 non-HP stat:
//   floor(floor((2*base + iv + ev/4) * level/100 + 5) * natureMult).
// EVs are 0 for everything this engine creates (no EV yields are imported),
// so this is the same number the IV-only form produced -- the term is here
// so an externally granted EV (a vitamin, a future import) actually counts.
static int calc_stat(int base, int iv, int ev, int level, float mult) {
	return (int)(((2 * base + iv + ev / 4) * level / 100 + 5) * mult);
}
// Gen-3 HP stat, same shape with the +level+10 tail and no nature term.
static int calc_hp(int base, int iv, int ev, int level) {
	return (2 * base + iv + ev / 4) * level / 100 + level + 10;
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
		if (c.size() >= 16) { s.item_common = c[14]; s.item_rare = c[15]; }
		species[c[0]] = s;
		species_index[c[0]] = (int)species_order.size();
		species_order.push_back(c[0]);
	}
	std::ifstream mv(dir + "/moves.tsv");
	while (std::getline(mv, line)) {
		auto c = split(line, '\t');
		if (c.size() < 4) continue;
		MoveInfo mi{std::stoi(c[1]), c[2], std::stoi(c[3]), "", 0, 20};
		if (c.size() >= 6) { mi.effect = c[4]; mi.secondary_chance = std::stoi(c[5]); }
		if (c.size() >= 7) mi.pp = std::stoi(c[6]);
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
	std::ifstream ti(dir + "/trainer_items.tsv");
	while (std::getline(ti, line)) {
		auto c = split(line, '\t');
		if (c.size() < 2 || c[1].empty()) continue;
		trainer_items_[c[0]] = split(c[1], ',');
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

bool BattleData::is_shiny(unsigned personality, unsigned ot_id, unsigned ot_secret) {
	// Test/demo hook, same family as main.cpp's other CODEMON_* env switches:
	// at the real 1/8192 odds a shiny encounter is unreachable in a scripted
	// headless run, so this makes every roll come out shiny.
	static const bool forced = std::getenv("CODEMON_FORCE_SHINY") != nullptr;
	if (forced) return true;
	unsigned v = (ot_id & 0xFFFFu) ^ (ot_secret & 0xFFFFu)
	           ^ ((personality >> 16) & 0xFFFFu) ^ (personality & 0xFFFFu);
	return v < SHINY_ODDS;
}

std::string BattleData::sprite_path(const std::string& species, bool shiny, bool back) {
	std::string p = "assets/pokemon/";
	if (shiny) p += "shiny/";
	if (back) p += "back/";
	return p + species + ".png";
}

Mon BattleData::make_mon(const std::string& name, int level, std::mt19937* rng,
                         unsigned ot_id, unsigned ot_secret) const {
	Mon mon; mon.species = name; mon.level = std::max(1, level);
	if (rng) {
		auto roll_iv = [&]() { return (int)((*rng)() % 32); };
		mon.iv_hp = roll_iv(); mon.iv_atk = roll_iv(); mon.iv_def = roll_iv();
		mon.iv_spa = roll_iv(); mon.iv_spd = roll_iv(); mon.iv_spe = roll_iv();
		mon.nature = NATURES[(*rng)() % 25].name;
		// One 32-bit personality value per individual, like pokeemerald's
		// CreateMon (Random32()); shininess falls straight out of it.
		mon.personality = (unsigned)((*rng)() & 0xFFFFFFFFu);
		mon.shiny = is_shiny(mon.personality, ot_id, ot_secret);
	}
	auto it = species.find(name);
	if (it == species.end()) { mon.max_hp = mon.hp = 10 + level; return mon; }
	const SpeciesInfo& s = it->second;
	const NatureRow* nr = nature_row(mon.nature);
	mon.max_hp = mon.hp = calc_hp(s.hp, mon.iv_hp, mon.ev_hp, mon.level);
	mon.atk = calc_stat(s.atk, mon.iv_atk, mon.ev_atk, mon.level, nature_mult(nr->atk));
	mon.def = calc_stat(s.def, mon.iv_def, mon.ev_def, mon.level, nature_mult(nr->def));
	mon.spa = calc_stat(s.spa, mon.iv_spa, mon.ev_spa, mon.level, nature_mult(nr->spa));
	mon.spd = calc_stat(s.spd, mon.iv_spd, mon.ev_spd, mon.level, nature_mult(nr->spd));
	mon.spe = calc_stat(s.spe, mon.iv_spe, mon.ev_spe, mon.level, nature_mult(nr->spe));
	mon.t1 = s.t1; mon.t2 = s.t2;
	mon.exp = exp_for_level(s.growth, mon.level);
	if (rng) {
		// Real pokeemerald wild-item roll (TryGenerateWildHeldItem, simplified:
		// no Compound Eyes / Altering Cave special cases): a species whose
		// common and rare items are the same non-NONE item always holds it,
		// otherwise 45% nothing / 50% common / 5% rare.
		if (s.item_common == s.item_rare && s.item_common != "NONE") {
			mon.held_item = s.item_common;
		} else {
			int rnd = (int)((*rng)() % 100);
			if (rnd < 45) mon.held_item = "NONE";
			else if (rnd < 95) mon.held_item = s.item_common;
			else mon.held_item = s.item_rare;
		}
	}

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
	for (const std::string& m : mon.moves) {
		const MoveInfo* mi = move(m);
		mon.pp.push_back(mi ? mi->pp : 20);
	}
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

const std::vector<std::string>& BattleData::trainer_items(const std::string& t) const {
	static const std::vector<std::string> empty;
	auto it = trainer_items_.find(t);
	return it == trainer_items_.end() ? empty : it->second;
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
	// Struggle bypasses type calculation entirely in the real games -- STAB
	// *and* the type chart, regardless of either side's types. It is data-typed
	// NORMAL, so without this a Ghost defender would zero it out and a mon with
	// no PP left could never damage (or take recoil from) one, leaving the
	// battle unable to reach an end state.
	bool is_struggle = (move_name == "STRUGGLE");
	float stab = (!is_struggle && (mi->type == atk.t1 || mi->type == atk.t2)) ? 1.5f : 1.0f;
	float eff = is_struggle ? 1.0f : type_eff(mi->type, def.t1, def.t2);
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
	const NatureRow* nr = nature_row(mon.nature);
	int new_max = calc_hp(s.hp, mon.iv_hp, mon.ev_hp, mon.level);
	if (keep_ratio) {
		float r = mon.max_hp > 0 ? (float)mon.hp / mon.max_hp : 1.f;
		mon.hp = std::max(1, (int)(new_max * r));
	} else {
		mon.hp += (new_max - mon.max_hp);      // level-up: gain the delta
	}
	mon.max_hp = new_max;
	mon.atk = calc_stat(s.atk, mon.iv_atk, mon.ev_atk, mon.level, nature_mult(nr->atk));
	mon.def = calc_stat(s.def, mon.iv_def, mon.ev_def, mon.level, nature_mult(nr->def));
	mon.spa = calc_stat(s.spa, mon.iv_spa, mon.ev_spa, mon.level, nature_mult(nr->spa));
	mon.spd = calc_stat(s.spd, mon.iv_spd, mon.ev_spd, mon.level, nature_mult(nr->spd));
	mon.spe = calc_stat(s.spe, mon.iv_spe, mon.ev_spe, mon.level, nature_mult(nr->spe));
	mon.t1 = s.t1; mon.t2 = s.t2;
}

void BattleData::restore_pp(Mon& mon) const {
	mon.pp.resize(mon.moves.size());
	for (size_t i = 0; i < mon.moves.size(); ++i) {
		const MoveInfo* mi = move(mon.moves[i]);
		mon.pp[i] = mi ? mi->pp : 20;
	}
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

std::pair<std::string, std::string> BattleData::species_types(const std::string& species_name) const {
	auto sp = species.find(species_name);
	if (sp == species.end()) return {"", ""};
	return {sp->second.t1, sp->second.t2};
}

std::string BattleData::ability(const std::string& species_name) const {
	auto sp = species.find(species_name);
	return sp == species.end() ? "NONE" : sp->second.ability1;
}

void BattleData::grant_exp(Mon& mon, long gained, std::vector<std::string>& msgs,
                           LevelUpReport* report) const {
	if (mon.fainted() || gained <= 0) return;
	auto sp = species.find(mon.species);
	std::string growth = sp == species.end() ? "MEDIUM_FAST" : sp->second.growth;
	mon.exp += gained;
	msgs.push_back(disp(mon.display_name()) + " erhält " + std::to_string(gained) + " EP!");
	while (mon.level < 100 && mon.exp >= exp_for_level(growth, mon.level + 1)) {
		mon.level++;
		if (report) report->levels_gained++;
		recompute_stats(mon, false);
		msgs.push_back(disp(mon.display_name()) + " erreicht Level " +
		               std::to_string(mon.level) + "!");
		// learn any move taught at this level
		auto li = learn.find(mon.species);
		if (li != learn.end()) {
			for (auto& lv : li->second) if (lv.first == mon.level) {
				const MoveInfo* nmi = move(lv.second);
				int new_pp = nmi ? nmi->pp : 20;
				// Already knows it (a move can appear twice in a learnset, and
				// an evolved form re-teaches moves the pre-evolution had).
				if (std::find(mon.moves.begin(), mon.moves.end(), lv.second) != mon.moves.end())
					continue;
				if (mon.moves.size() < 4) {
					mon.moves.push_back(lv.second);
					mon.pp.push_back(new_pp);
					msgs.push_back(disp(mon.display_name()) + " erlernt " + disp(lv.second) + "!");
					if (report) report->learned.push_back(lv.second);
				} else if (report) {
					// Four moves already: the real games stop and ask which one
					// to forget. Defer to the caller rather than deciding here.
					report->pending_moves.push_back(lv.second);
				} else {
					msgs.push_back(disp(mon.display_name()) + " erlernt " + disp(lv.second) +
					               " (vergisst " + disp(mon.moves[0]) + ")");
					mon.moves[0] = lv.second;
					if (!mon.pp.empty()) mon.pp[0] = new_pp; else mon.pp.push_back(new_pp);
				}
			}
		}
		// level-up evolution -- Everstone blocks it, same as the real games.
		auto ei = evos.find(mon.species);
		if (ei != evos.end() && mon.held_item != "EVERSTONE") {
			for (const Evolution& e : ei->second) {
				if (e.method.rfind("LEVEL", 0) == 0 &&
				    mon.level >= std::atoi(e.param.c_str())) {
					std::string from_id = mon.species, from = disp(mon.species);
					mon.species = e.target;
					recompute_stats(mon, true);
					msgs.push_back(from + " entwickelt sich zu " + disp(mon.species) + "!");
					if (report) { report->evolved_from = from_id; report->evolved_to = e.target; }
					break;
				}
			}
		}
	}
}

// ------------------------------------------------------------------ gender --
// The species that have no gender at all, or only ever one. Everything else
// falls back to the 50/50 personality split -- see the header for why the real
// per-species ratio isn't available here.
static bool species_in(const std::string& s, const char* const* list) {
	for (const char* const* p = list; *p; ++p) if (s == *p) return true;
	return false;
}
char BattleData::gender(const std::string& sp, unsigned personality) {
	static const char* GENDERLESS[] = {
		"MAGNEMITE", "MAGNETON", "VOLTORB", "ELECTRODE", "STARYU", "STARMIE",
		"DITTO", "PORYGON", "PORYGON2", "ARTICUNO", "ZAPDOS", "MOLTRES",
		"MEWTWO", "MEW", "UNOWN", "LUNATONE", "SOLROCK", "BALTOY", "CLAYDOL",
		"BELDUM", "METANG", "METAGROSS", "REGIROCK", "REGICE", "REGISTEEL",
		"LATIOS", "KYOGRE", "GROUDON", "RAYQUAZA", "JIRACHI", "DEOXYS",
		"RAIKOU", "ENTEI", "SUICUNE", "LUGIA", "HO_OH", "CELEBI", "SHEDINJA",
		"CLAMPERL", "HUNTAIL", "GOREBYSS", nullptr
	};
	static const char* ALWAYS_FEMALE[] = {
		"NIDORAN_F", "NIDORINA", "NIDOQUEEN", "CHANSEY", "BLISSEY", "KANGASKHAN",
		"JYNX", "MILTANK", "SMOOCHUM", "ILLUMISE", "LATIAS", nullptr
	};
	static const char* ALWAYS_MALE[] = {
		"NIDORAN_M", "NIDORINO", "NIDOKING", "HITMONLEE", "HITMONCHAN",
		"HITMONTOP", "TAUROS", "TYROGUE", "VOLBEAT", nullptr
	};
	if (species_in(sp, GENDERLESS)) return 'N';
	if (species_in(sp, ALWAYS_FEMALE)) return 'F';
	if (species_in(sp, ALWAYS_MALE)) return 'M';
	// pokeemerald reads the low byte of the personality value against the
	// species' gender ratio; with no ratio imported this is the even split.
	return (personality & 0xFFu) < 127u ? 'F' : 'M';
}

const char* BattleData::gender_symbol(char g) {
	if (g == 'M') return "\u2642";
	if (g == 'F') return "\u2640";
	return "";
}

const char* summary_page_title(SummaryPage p) {
	switch (p) {
	case SummaryPage::OVERVIEW: return "ÜBERSICHT";
	case SummaryPage::MOVES:    return "ATTACKEN";
	case SummaryPage::STATS:    return "STATUSWERTE";
	case SummaryPage::DETAILS:  return "DETAILS";
	case SummaryPage::RIBBONS:  return "BÄNDER";
	default:                    return "";
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
