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
	std::string growth = "MEDIUM_FAST";
	int exp_yield = 50;
	int catch_rate = 45;   // pokeemerald's real per-species catchRate (3..255)
	// pokeemerald's real .abilities -- a species with only one ability has
	// ability2 == "NONE". Individual pokemon in the real games pick between
	// the two based on a personality-value bit; this engine always uses
	// ability1, a documented simplification (see Mon::ability).
	std::string ability1 = "NONE", ability2 = "NONE";
	// pokeemerald's real .itemCommon/.itemRare -- the wild held-item pool
	// rolled once per individual in make_mon() (see Mon::held_item).
	std::string item_common = "NONE", item_rare = "NONE";
};

// A level-up / stone / trade evolution rule.
struct Evolution {
	std::string method;   // e.g. "LEVEL", "ITEM", "LEVEL_SILCOON"
	std::string param;    // level number or item constant
	std::string target;   // resulting species (no SPECIES_ prefix)
};

// Major status conditions -- Gen-3 rule: at most one of these at a time.
// Confusion is tracked separately on Mon (it's volatile and can coexist
// with any of these, e.g. a poisoned mon can also be confused).
enum class Status { NONE, SLEEP, POISON, TOXIC, BURN, PARALYSIS, FREEZE };

struct MoveInfo {
	int power;
	std::string type;
	int accuracy;               // 0 = never misses (Swift, self-target moves, ...)
	std::string effect;         // bare EFFECT_* suffix, e.g. "SLEEP", "BURN_HIT"
	int secondary_chance = 0;   // % chance an EFFECT_*_HIT's secondary status lands
	int pp = 20;                // pokeemerald's real per-move max PP
};

// A battle-ready pokemon with computed stats and up to four moves.
struct Mon {
	std::string species;
	int level = 5;
	int max_hp = 1, hp = 1;
	int atk = 1, def = 1, spa = 1, spd = 1, spe = 1;
	std::string t1 = "NORMAL", t2 = "NORMAL";
	std::vector<std::string> moves;
	std::vector<int> pp;         // current PP, parallel to `moves` (max PP on learn)
	long exp = 0;
	Status status = Status::NONE;
	int status_turns = 0;       // SLEEP: turns left asleep; TOXIC: turns badly poisoned so far
	int confusion_turns = 0;    // 0 = not confused
	// Individual variation (real Gen-3 mechanics): IVs 0..31 per stat and one
	// of 25 natures, both rolled once in make_mon() and kept for this mon's
	// whole life (persisted to the savegame, unlike a battle's stat stages).
	// EVs live further down and take part in the same formula; nothing grants
	// them during normal play, since species.tsv carries no EV yields.
	int iv_hp = 15, iv_atk = 15, iv_def = 15, iv_spa = 15, iv_spd = 15, iv_spe = 15;
	std::string nature = "HARDY";
	// Real per-species wild held-item roll (pokeemerald's 45% none / 50%
	// common / 5% rare split, see make_mon()), kept for this individual's
	// whole life like IVs/nature. "NONE" if it isn't holding anything.
	std::string held_item = "NONE";
	// pokeemerald's personality value: the 32-bit number rolled once per
	// individual in make_mon() that the real games derive a mon's "random
	// but fixed forever" traits from. Only shininess is read off it here
	// (see BattleData::is_shiny) -- gender, the ability-1/2 pick and Unown's
	// letter are documented simplifications elsewhere in this file.
	unsigned personality = 0;
	// Derived from `personality` against the player's trainer ID pair at
	// creation time and then kept for life (a mon doesn't stop being shiny
	// when it's caught, traded into the party or evolves). Only changes
	// which sprite set is drawn -- shininess has no effect on stats, exactly
	// like the real games.
	bool shiny = false;

	// --- identity & bookkeeping (PartySystem) ------------------------------
	// A process-unique handle for this individual, minted by PartySystem
	// (see PartySystem::adopt) and persisted with the savegame. The party and
	// the PC boxes are two views onto the same population, so "move to box"
	// has to be a change of which container holds the mon, never a copy --
	// this id is what lets the rest of the engine say "that one" without
	// caring which container it currently sits in. 0 = never adopted.
	unsigned uid = 0;
	// Player-given name; empty means "show the species name", exactly like a
	// real games' mon that was never nicknamed.
	std::string nickname;
	// Original Trainer. ot_id/ot_secret are the pair the shiny check already
	// used at creation time (see `shiny`), kept per-mon so a traded-in mon
	// keeps its own OT rather than inheriting the current player's.
	std::string ot_name;
	unsigned ot_id = 0, ot_secret = 0;
	// The ball this mon was caught in (ITEM_POKE_BALL, ITEM_NET_BALL, ...);
	// "NONE" for a mon that was never caught (a starter, a gift, a hatched
	// egg in games that have them).
	std::string ball = "NONE";
	// Where and at what level it joined the player (pokeemerald's metLocation
	// /metLevel). Empty/0 for a mon whose origin was never recorded.
	std::string met_location;
	int met_level = 0;
	// pokeemerald's friendship byte: 70 for most caught mons, 120 for a
	// starter/gift. Nothing consumes it yet (Return/Frustration and
	// friendship evolutions are out of scope); it is tracked, saved and
	// shown so the data model matches the real one.
	int friendship = 70;
	// Effort values, 0..255 per stat, capped at 510 total -- part of the real
	// stat formula (see calc_stat). This engine has no EV *yields* imported
	// from the species table, so nothing grants them during normal play; they
	// exist so externally granted EVs (vitamins, a future import) compute
	// correctly and survive a save/load round trip.
	int ev_hp = 0, ev_atk = 0, ev_def = 0, ev_spa = 0, ev_spd = 0, ev_spe = 0;
	// Ribbons/badges this individual earned, as bare RIBBON_* style ids.
	std::vector<std::string> ribbons;

	bool fainted() const { return hp <= 0; }
	// What the party/summary screens should call this mon.
	std::string display_name() const { return nickname.empty() ? species : nickname; }
	// Total EVs, for the real games' 510 cap (see PartySystem::add_ev).
	int ev_total() const { return ev_hp + ev_atk + ev_def + ev_spa + ev_spd + ev_spe; }
};

// Which page of a summary screen is being shown. The real games' summary is
// paged rather than one long list, and each page answers a different question
// -- kept here (not in Menu) so a second summary surface (a box screen, a
// battle-time "check" view) shows the same pages in the same order.
enum class SummaryPage { OVERVIEW, MOVES, STATS, DETAILS, RIBBONS, COUNT };
const char* summary_page_title(SummaryPage p);

class BattleData
{
private:
	std::unordered_map<std::string, SpeciesInfo> species;
	std::vector<std::string> species_order;                       // load() file order, stable
	std::unordered_map<std::string, int> species_index;            // name -> index into species_order
	std::unordered_map<std::string, MoveInfo> moves;
	std::unordered_map<std::string, std::vector<std::pair<int, std::string>>> learn;
	std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> trainers;
	std::unordered_map<std::string, std::string> trainer_pics;   // TRAINER_X -> pic
	// TRAINER_X -> the trainer's own AI healing-item pool (Full Restore/
	// Hyper Potion/...; real pokeemerald's trainers.h `.items` field, not a
	// mon's held item). Only Gym Leaders/rivals/Elite Four etc. carry any.
	std::unordered_map<std::string, std::vector<std::string>> trainer_items_;
	std::unordered_map<std::string, std::vector<Evolution>> evos;
	std::unordered_map<std::string, std::unordered_map<std::string, bool>> tm_learn;
	std::unordered_map<std::string, std::string> tm_move;         // "HM01" -> "CUT"
	bool loaded = false;

public:
	bool load(const std::string& dir = "assets/battle");
	bool is_loaded() const { return loaded; }

	bool has_species(const std::string& s) const { return species.count(s) > 0; }
	const MoveInfo* move(const std::string& m) const;

	// A stable-within-this-run numeric id for a species (pokeemerald's own
	// SPECIES_* constants have no equivalent here since every species table
	// is keyed by bare name) -- for opcodes like `bufferspeciesname` that
	// pokeemerald passes/stores a species as a plain number (in-game trades'
	// `GetTradeSpecies`/`GetInGameTradeSpeciesInfo`, ...). -1 if unknown.
	int species_id(const std::string& name) const;
	std::string species_by_id(int id) const;   // "" if out of range
	// Total imported species -- the Pokédex screen walks 0..species_count()-1
	// via species_by_id(). This is species.tsv's own (alphabetical) order,
	// not the real games' curated Hoenn Dex numbering (not imported).
	int species_count() const { return (int)species_order.size(); }

	// Build a level-scaled pokemon with its natural (level-up) moveset, real
	// IVs (0..31 per stat), a random nature and a random personality value
	// when `rng` is given -- nullptr falls back to neutral IVs (15)/nature
	// (Hardy) and a never-shiny personality of 0, for callers without one
	// handy rather than a hard dependency. ot_id/ot_secret are the owning
	// player's trainer ID pair (GameState::trainer_id/secret_id); they only
	// feed the shiny check, which stays at its real 1/8192 either way, so
	// callers with no GameState in reach can leave them at 0.
	Mon make_mon(const std::string& species_name, int level, std::mt19937* rng = nullptr,
	             unsigned ot_id = 0, unsigned ot_secret = 0) const;

	// pokeemerald's GET_SHINY_VALUE / SHINY_ODDS (include/pokemon.h): a mon
	// is shiny when the two halves of its owner's 32-bit OT id (public
	// trainer ID in the low half, secret ID in the high half) and the two
	// halves of its personality value XOR to less than 8 -- 8 of 65536
	// personality values, the real games' 1/8192 chance.
	static const unsigned SHINY_ODDS = 8;
	static bool is_shiny(unsigned personality, unsigned ot_id, unsigned ot_secret);

	// 'M', 'F' or 'N' (genderless). pokeemerald derives this from the
	// species' genderRatio against the low byte of the personality value;
	// species.tsv carries no gender ratio (it isn't in the imported columns),
	// so this uses the same personality byte against a plain 50/50 split,
	// with a table for the species that are genuinely genderless or
	// single-gender. Documented simplification: a species with a skewed but
	// non-absolute ratio (Bulbasaur's 87.5% male, ...) comes out 50/50 here.
	static char gender(const std::string& species, unsigned personality);
	static const char* gender_symbol(char g);   // "♂"/"♀"/"" for the UI

	// Where a species' 64x64 battle artwork lives. On the GBA a shiny mon is
	// the same pixels read through a second 16-colour palette; the importer
	// bakes that second palette into a mirrored assets/pokemon/shiny/ tree
	// (see tools/pe_import.py), so picking a sprite is just picking a folder.
	// Shared by the battle screen and the menu's party/summary/PC icons.
	static std::string sprite_path(const std::string& species, bool shiny,
	                               bool back = false);

	// Trainer's party, or empty if unknown.
	std::vector<std::pair<std::string, int>> trainer_party(const std::string& t) const;
	// Trainer's front-pic file stem (e.g. "hiker"), or "" if unknown.
	std::string trainer_pic(const std::string& t) const;
	// Trainer's own AI healing-item pool (ITEM_* ids, real pokeemerald
	// order), or empty for the ~80% of trainers who don't carry any.
	const std::vector<std::string>& trainer_items(const std::string& t) const;

	// Type effectiveness multiplier of an attacking type vs a defender's types.
	static float type_eff(const std::string& atk,
	                      const std::string& d1, const std::string& d2);
	// Is a move type physical (uses Atk/Def) in the Gen-3 split?
	static bool is_physical(const std::string& type);
	// Status condition a move's bare EFFECT_* suffix inflicts, or NONE if
	// it isn't a status-inflicting effect (most aren't -- weather, stat
	// stages (see Battle::apply_stat_change) etc. are handled elsewhere or
	// out of scope).
	static Status effect_status(const std::string& effect);
	// EFFECT_CONFUSE / EFFECT_CONFUSE_HIT: the volatile confusion status
	// (tracked separately from `status` since it can coexist with one).
	static bool effect_confuses(const std::string& effect);
	static const char* status_name(Status s);   // "PSN", "PAR", ... for the UI

	// Damage of `attacker` using `move_name` against `defender`. atk_mult/
	// def_mult are the caller's own Gen-3 stat-stage multipliers (see
	// Battle::stage_mult) applied to the attack/defense stat this move's
	// physical/special split actually uses; both default to 1 (no stages).
	// `crit` doubles the result (the caller is expected to have already
	// picked atk_mult/def_mult per the real games' crit-ignores-stages rule).
	int damage(const Mon& attacker, const Mon& defender,
	           const std::string& move_name, std::mt19937& rng,
	           float atk_mult = 1.f, float def_mult = 1.f, bool crit = false) const;

	// --- progression -------------------------------------------------------
	// Total experience needed to be at `level` for a growth-rate name.
	static long exp_for_level(const std::string& growth, int level);
	int exp_yield(const std::string& species) const;
	// This species' growth-rate curve name (for exp_for_level), defaulting
	// to MEDIUM_FAST for an unknown species -- same fallback grant_exp uses.
	// Lets UI code (the battle HUD's EXP bar, the party screen's) compute a
	// mon's progress to next level without needing BattleData internals.
	std::string growth_rate(const std::string& species) const;
	// pokeemerald's real per-species catch rate (3 for legendaries like
	// Mewtwo/Registeel up to 255 for Magikarp/Rattata); 45 for an unknown
	// species (the same default most early/mid-tier Pokemon actually have).
	int catch_rate(const std::string& species) const;
	// This species' two types ("" for the second if it has only one) --
	// used by Battle's per-ball catch multipliers (e.g. Net Ball vs
	// Water/Bug). Both empty for an unknown species.
	std::pair<std::string, std::string> species_types(const std::string& species) const;
	// This species' real pokeemerald ability (always ability1 -- a real
	// individual picks between ability1/ability2 off a personality-value
	// bit, which this engine doesn't model; "NONE" for both an unknown
	// species and one whose sole ability slot genuinely is empty).
	std::string ability(const std::string& species) const;

	// Recompute stats from base for the mon's current species+level; when
	// keep_ratio the current HP is scaled to the new max, else the HP delta is
	// added (level-up behaviour).
	void recompute_stats(Mon& mon, bool keep_ratio) const;
	// Restore every move's PP to max (Pokémon Center full heal).
	void restore_pp(Mon& mon) const;

	// What a grant_exp() call actually did, for callers that need to react to
	// it rather than just print its messages (PartySystem raises its
	// PokemonLevelUp/LearnedMove/Evolution* events off this).
	struct LevelUpReport {
		int levels_gained = 0;
		std::vector<std::string> learned;         // moves that fit and were learned
		// Level-up moves that did NOT fit because the mon already knows four.
		// With a report in hand grant_exp defers them here instead of silently
		// overwriting move slot 0, so the caller can ask the player which move
		// to replace (the real games' "should a move be forgotten?" prompt).
		std::vector<std::string> pending_moves;
		std::string evolved_from, evolved_to;     // empty unless it evolved
	};

	// Grant experience; applies level-ups (stat gains, level-up moves learned,
	// evolutions). Any player-facing lines are appended to `msgs`. `report`
	// is optional: passing one both records what happened and switches the
	// full-moveset case from "overwrite slot 0" to "defer to the caller"
	// (see LevelUpReport::pending_moves).
	void grant_exp(Mon& mon, long gained, std::vector<std::string>& msgs,
	               LevelUpReport* report = nullptr) const;

	// TM support.
	// "CUT" -> "HM01" (the bag's TM/HM number label); "" if not a TM/HM
	// move. The move itself comes straight off the bag item's own name
	// (ITEM_TM_<move>/ITEM_HM_<move>, see Menu.cpp) -- this is only for
	// display, matching the real games showing "TM01" rather than the
	// move name in the bag list.
	std::string move_to_tm_code(const std::string& move) const;
	bool can_learn_tm(const std::string& species, const std::string& move) const;
};
