#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>

/******************************************************************************
GameState - persistent flags, variables and bag shared across maps.

pokeemerald scripts read and write named flags (FLAG_*) and variables (VAR_*)
and hand items to the player. Those live here so state survives map changes and
conditional dialog behaves (e.g. a shopkeeper who talks differently the second
time). Flags are stored as 0/1 variables in the same table.
*****************************************************************************/
class GameState
{
private:
	std::unordered_map<std::string, int> vars;   // FLAG_* and VAR_*
	std::unordered_map<std::string, int> bag;     // ITEM_* -> count

public:
	int get_var(const std::string& name) const {
		auto it = vars.find(name);
		return it == vars.end() ? 0 : it->second;
	}
	void set_var(const std::string& name, int value) { vars[name] = value; }

	bool flag(const std::string& name) const { return get_var(name) != 0; }
	void set_flag(const std::string& name) { vars[name] = 1; }
	void clear_flag(const std::string& name) { vars[name] = 0; }

	// For SaveGame: every FLAG_*/VAR_* currently set (0-valued ones are kept
	// out by set_var/set_flag never storing a meaningful 0, so this is exactly
	// what needs to survive a save/load round-trip).
	const std::unordered_map<std::string, int>& all_vars() const { return vars; }

	// Poke Dollars. pokeemerald starts a new save with 3000; scripts read/
	// write it only through native code (AddMoney/RemoveMoney specials),
	// never a plain VAR_*, so it gets its own home instead of living in `vars`.
	int money = 3000;

	// Whiteout recovery point (pokeemerald's gSaveBlock1Ptr->lastHealLocation):
	// updated every time the player heals at a Pokemon Center, read when a
	// lost battle needs somewhere to send them back to.
	std::string last_heal_map;
	int last_heal_x = -1, last_heal_y = -1;

	// Player identity, chosen once via GenderSelect/NameEntry at the start of
	// a new game (see main.cpp): `female` drives checkplayergender and which
	// overworld sprite sheet loads; player_name/rival_name replace the
	// literal "PLAYER"/"RIVAL" tokens dialogue text carries (pe_import.py
	// turns pokeemerald's {PLAYER}/{RIVAL} escape codes into those literal
	// words on import; ScriptVM substitutes them back at msgbox time).
	bool female = false;
	std::string player_name = "BRENDAN";
	std::string rival_name = "MAY";
	// Dewford's "trendy saying": the phrase its townspeople will not stop
	// talking about, and which every one of their lines interpolates as
	// STR_VAR_1. In pokeemerald it is an Easy Chat phrase the player can
	// replace at the Hall; this engine has no Easy Chat screen, so it stays
	// at its starting value -- without it those lines showed the raw token.
	std::string trendy_phrase = "SUPER COOL";

	// The player's trainer ID pair (pokeemerald's gSaveBlock2Ptr->
	// playerTrainerId): the visible 5-digit ID plus the hidden "secret ID",
	// rolled once when a new game starts. Nothing displays them yet -- they
	// exist because together with a Pokemon's personality value they decide
	// which individuals are shiny (BattleData::is_shiny), so they have to be
	// the same on every load or a caught shiny would stop being one.
	unsigned trainer_id = 0, secret_id = 0;

	// AUFGABEN (QuestLog.h): which quest the player pinned to the HUD, and
	// whether the "NÄCHSTES ZIEL" box is shown at all. A quest's own progress
	// is never stored -- it is derived from the flags/vars below every frame --
	// so these two lines of player preference are the whole quest save state.
	// An id naming a quest that no longer exists (an edited quests.txt) simply
	// falls back to the current main mission, so it can never wedge the HUD.
	std::string tracked_quest;
	bool quest_hud_on = true;

	// Options screen (a curated subset of pokeemerald's real SaveBlock2
	// options, see the OPTIONS screen in Menu.cpp/main.cpp and README for
	// what's simplified/skipped -- Text Speed and Battle Style aren't
	// implemented, there's no typewriter text-reveal or switch-prompt
	// mechanic in this engine to hook them into).
	bool sound_on = true;           // Audio::set_muted(!sound_on)
	bool battle_scene_on = true;    // Battle::tick() skips the hit-shake/flash animation when off
	int frame_type = 0;             // 0-based index into text_window/1..20.png (see UiFrame::load_type)

	// `setdynamicwarp MAP x y` (script): records a destination for the next
	// step onto a WARP_ID_DYNAMIC tile (dest "-" in the .map file) -- used by
	// e.g. the intro moving-truck's exit and the Lilycove department store
	// elevator, where the same physical warp tile has to go somewhere
	// different depending on prior script state. Not part of the save file
	// (matches pokeemerald: it's a plain global, re-set by script before
	// every use, never read across a save/load boundary).
	std::string dynamic_warp_map;
	int dynamic_warp_x = -1, dynamic_warp_y = -1;

	void give_item(const std::string& item, int amount) { bag[item] += amount; }
	// Remove up to `amount` of an item; erases the entry when it hits zero.
	void take_item(const std::string& item, int amount) {
		auto it = bag.find(item);
		if (it == bag.end()) return;
		it->second -= amount;
		if (it->second <= 0) bag.erase(it);
	}
	int item_count(const std::string& item) const {
		auto it = bag.find(item);
		return it == bag.end() ? 0 : it->second;
	}
	const std::unordered_map<std::string, int>& bag_items() const { return bag; }

	// Pokédex seen/caught (pokeemerald's per-species dex flags).
	void mark_seen(const std::string& species) { pokedex_seen.insert(species); }
	void mark_caught(const std::string& species) { pokedex_caught.insert(species); }
	bool is_caught(const std::string& species) const { return pokedex_caught.count(species) > 0; }
	bool is_seen(const std::string& species) const {
		return is_caught(species) || pokedex_seen.count(species) > 0;
	}
	// For SaveGame: every seen (not-yet-caught) and every caught species.
	const std::unordered_set<std::string>& pokedex_seen_set() const { return pokedex_seen; }
	const std::unordered_set<std::string>& pokedex_caught_set() const { return pokedex_caught; }

private:
	std::unordered_set<std::string> pokedex_seen, pokedex_caught;
};
