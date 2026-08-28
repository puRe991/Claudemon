#pragma once
#include <string>
#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include "SFML/Graphics.hpp"
#include "BattleData.h"
#include "GameState.h"
#include "UiFrame.h"
#include "Audio.h"

/******************************************************************************
Battle - a turn-based pokemon battle for wild encounters and trainer fights.

Renders the enemy front sprite + the player's back sprite, HP bars, a battle
log and a 2x2 move menu, and resolves turns with the BattleData damage model
(order by Speed, type effectiveness, faint, trainer party switching).

Input is fed one button at a time; the game loop draws it and reads active().
The player's Mon is mutated in place so HP carries across battles.
*****************************************************************************/
enum BtnInput { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CONFIRM, BTN_CANCEL };

class Battle
{
public:
	// Matches pokeemerald's B_OUTCOME_* constants closely enough for
	// ScriptVM's `specialvar VAR, GetBattleOutcome` to report a real result.
	enum Outcome { OUTCOME_NONE = 0, OUTCOME_WON = 1, OUTCOME_LOST = 2,
	               OUTCOME_RAN = 4, OUTCOME_CAUGHT = 7 };

private:
	enum Phase { INACTIVE, MSG, ACTION, MOVE, SWITCH };
	// Simplified flat rate standing in for pokeemerald's per-trainer-class
	// money field (not imported -- trainers.tsv only carries id + party).
	static const int PRIZE_MONEY_PER_LEVEL = 20;

	BattleData* data;
	std::mt19937* rng;
	GameState* gs;
	Audio* audio = nullptr;
	std::vector<Mon>* team;
	std::vector<Mon>* box;
	int action_cursor;
	sf::Font font; bool font_ok;
	UiFrame frame;

	Mon* player;                 // owned by caller; mutated here
	size_t active_idx;            // player's index into *team
	Mon enemy;
	bool is_trainer;
	std::string enemy_title;     // "" for wild, trainer display name otherwise
	std::vector<std::pair<std::string, int>> party;   // remaining trainer mons
	size_t party_idx;
	// The trainer's own AI healing-item pool for this battle (real
	// pokeemerald `.items` field, see BattleData::trainer_items) -- consumed
	// as used, resets fresh on the next start_trainer() call. Empty for wild
	// battles and the ~80% of trainers who don't carry any.
	std::vector<std::string> enemy_items;
	// If the active enemy mon is hurting enough and the trainer still has a
	// usable item, use it instead of attacking this turn (queues the
	// message, heals/cures, removes the item from the pool). Returns false
	// (does nothing) for wild battles, an empty pool, or a mon that's fine.
	bool try_use_enemy_item();

	int switch_cursor;            // selected row in the SWITCH party list
	bool forced_switch;           // true when the active mon just fainted (no cancel)
	bool has_healthy_reserve() const;
	// Common "active mon just fainted" handling: force a switch if the team
	// has another healthy member, else end the battle in a loss.
	void handle_player_faint();
	void open_switch();           // POKéMON chosen from the action menu
	void do_switch(int idx);      // send out team[idx]

	std::vector<std::string> log;
	size_t log_pos;
	Phase phase;
	Phase after_msg;             // where to go once the log is exhausted (MENU/INACTIVE)
	int cursor;
	bool over, victory;
	Outcome last_outcome;

	sf::Texture enemy_tex, player_tex, trainer_tex;
	bool has_trainer_pic;
	bool intro_shown;            // trainer sprite shown until the first menu
	std::map<std::string, sf::Texture> type_tex;

	// --- stat stages ---------------------------------------------------------
	// Gen-3 in-battle stat stages (-6..+6), reset whenever that side's active
	// mon changes (a fresh wild/trainer battle, a trainer's next mon, or the
	// player switching) -- never persisted on Mon itself, same as real games.
	struct StatStages { int atk = 0, def = 0, spa = 0, spd = 0, spe = 0, acc = 0, eva = 0, crit = 0; };
	StatStages player_stages, enemy_stages;
	StatStages& stages_for(Mon& m) { return &m == this->player ? this->player_stages : this->enemy_stages; }
	// -6..+6 -> Gen-3 multiplier (2/8 .. 8/2 for atk/def/spa/spd/spe, 3/9..9/3
	// for accuracy/evasion).
	static float stage_mult(int stage);
	static float acc_stage_mult(int stage);
	// A stat-changing move's effect (Growl, Swords Dance, Bulk Up, Haze, ...)
	// applied to the right side (self-buffs target `atk`, debuffs target
	// `def`) with the real "X's DEFENSE fell!"/"won't go any higher!"
	// messages. False if `effect` isn't a stat-stage effect at all, so the
	// caller falls through to try_inflict_status for status/confusion.
	bool apply_stat_change(Mon& atk, Mon& def, const std::string& effect);

	// --- weather ---------------------------------------------------------
	// Gen-3 battle weather: RAIN_DANCE/SUNNY_DAY/SANDSTORM/HAIL, 5 turns,
	// the latest move overwrites whatever was active. Never carries over
	// between battles.
	enum Weather { WEATHER_NONE, WEATHER_RAIN, WEATHER_SUN, WEATHER_SANDSTORM, WEATHER_HAIL };
	Weather weather = WEATHER_NONE;
	int weather_turns = 0;
	// Sets weather from a move's effect string; false if `effect` isn't one
	// of the four weather-setting effects.
	bool apply_weather_effect(const std::string& effect);
	// Water/Fire damage multiplier for the current weather (1.5x/0.5x in
	// rain/sun, 1 otherwise) -- folded into the attacker's own stage
	// multiplier before calling BattleData::damage().
	float weather_damage_mult(const std::string& move_type) const;

	// --- abilities (a hand-picked, commonly-relevant subset -- not all 77
	// Gen-3 abilities, see README) ------------------------------------------
	// Fires whenever `incoming` becomes the active mon on its side (battle
	// start, a trainer's next mon, or the player switching): Intimidate
	// drops the opposing side's Attack a stage, Drizzle/Drought/Sand Stream
	// set their weather (a long, not-quite-infinite duration, since this
	// engine has no separate "ability weather never expires" concept).
	void on_switch_in(Mon& incoming);

	// Gen-3 critical-hit roll: stage 0 (1/16) bumped +1 by a HIGH_CRITICAL
	// move, +2 by a prior Focus Energy (StatStages::crit) -- stage 4+ caps
	// at 1/2. A crit ignores the attacker's own negative stage and the
	// defender's positive stage (real games' rule), applied by the caller.
	bool roll_critical(const std::string& move_effect, int crit_stage) const;
	// This move's Gen-3 turn-order priority bracket (+1 Quick Attack-effect
	// moves, -1 Vital Throw, 0 everything else) -- resolved before Speed.
	int move_priority(const std::string& mv) const;

	const sf::Texture* type_icon(const std::string& type);
	void load_sprites();
	void queue(const std::string& line);
	void show_messages(Phase next);
	std::string ai_move() const;
	void do_move(Mon& atk, Mon& def, const std::string& mv,
	             const std::string& atk_name);
	void resolve_turn(const std::string& player_move);
	void throw_ball();
	void flee();
	void enemy_turn_after();     // enemy attacks once, then back to ACTION / end
	void send_next_enemy();
	void handle_enemy_faint();   // award EXP, next trainer mon, or victory
	static std::string nice(const std::string& id);   // POKEMON_NAME -> Pokemon Name

	// --- status conditions --------------------------------------------------
	// acc_stage/eva_stage default to 0 (neutral) for callers that don't track
	// stages themselves (none left, but keeps this usable standalone).
	bool roll_accuracy(int accuracy, int acc_stage = 0, int eva_stage = 0) const;
	// True if `m` couldn't act this turn (sleep/freeze/full paralysis/a
	// confusion self-hit) -- queues the matching message either way.
	bool status_blocks_turn(Mon& m);
	// Apply a move's status effect (or confusion) to `target`, honoring the
	// Gen-3 one-major-status-at-a-time rule and type immunities.
	void try_inflict_status(Mon& target, const std::string& effect);
	// End-of-turn poison/burn/toxic damage for both sides.
	void apply_end_of_turn_effects();

	// --- PP / Struggle -------------------------------------------------------
	// Decrements m's PP for `mv` if it's one of its own known moves (a no-op
	// for Struggle, which isn't in anyone's movelist and has unlimited use).
	static void consume_pp(Mon& m, const std::string& mv);
	// True if every one of m's moves is at 0 PP (Struggle time).
	static bool out_of_pp(const Mon& m);

	// --- held items (a hand-picked subset -- see README) --------------------
	// Cures a just-inflicted status/confusion via a held status berry
	// (Cheri/Chesto/Pecha/Rawst/Aspear/Persim/Lum), consuming it and queuing
	// a message. Called right after try_inflict_status sets the status.
	void check_status_berry(Mon& m);
	// Heals via a held Oran/Sitrus berry once HP drops to <=50% max, then
	// consumes it. Called after every deal_damage() on that mon.
	void check_pinch_berry(Mon& m);
	// +10% damage for one held type-boosting item (Mystic Water and the
	// rest of the real Gen-3 set), folded in alongside weather/STAB.
	static float held_item_type_mult(const Mon& atk, const std::string& move_type);

public:
	Battle();
	void configure(BattleData* d, std::mt19937* r);
	// where caught pokemon go, and the bag for Poke Balls
	void set_capture(GameState* g, std::vector<Mon>* team, std::vector<Mon>* box);
	// Optional (headless tests run without one): battle music + cries.
	void set_audio(Audio* a) { this->audio = a; }

	bool start_wild(const std::string& species, int level, Mon* player_mon);
	bool start_trainer(const std::string& trainer_id, const std::string& name,
	                   Mon* player_mon);

	bool active() const { return phase != INACTIVE; }
	bool won() const { return victory; }
	Outcome outcome() const { return last_outcome; }

	// Prize money for beating this trainer (0 for a wild battle). Real
	// pokeemerald pays trainerClass.money * the last Pokemon's level; this
	// engine has no imported trainer-class data, so it approximates with a
	// flat per-level rate against the trainer's highest-level Pokemon.
	int prize_money() const {
		if (!this->is_trainer || this->party.empty()) return 0;
		int highest = 0;
		for (const auto& p : this->party) highest = std::max(highest, p.second);
		return highest * PRIZE_MONEY_PER_LEVEL;
	}

	// Introspection for headless drivers/tests -- there's no other way to
	// observe the enemy mon's live state (it's not the caller's Mon like
	// `player` is).
	int enemy_hp() const { return this->enemy.hp; }
	int enemy_max_hp() const { return this->enemy.max_hp; }
	Status enemy_status() const { return this->enemy.status; }
	size_t enemy_items_left() const { return this->enemy_items.size(); }

	// Which sub-screen the battle is currently showing. Exposed so a headless
	// driver (the engine tests, a CODEMON_WALK script) can step the menus off
	// the real state instead of guessing how many presses a message queue
	// needs -- miscounting there silently re-enters the move menu and picks a
	// different move than intended.
	enum Screen { SCR_INACTIVE, SCR_MESSAGE, SCR_ACTION, SCR_MOVE, SCR_SWITCH };
	Screen screen() const {
		switch (phase) {
			case MSG:    return SCR_MESSAGE;
			case ACTION: return SCR_ACTION;
			case MOVE:   return SCR_MOVE;
			case SWITCH: return SCR_SWITCH;
			default:     return SCR_INACTIVE;
		}
	}

	void input(BtnInput b);
	void tick(float dt);          // drives the hit shake animation
	void draw(sf::RenderTarget& target);

private:
	float shake_t; int shake_side; int prev_ehp, prev_php;
};
