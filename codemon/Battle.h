#pragma once
#include <string>
#include <vector>
#include <map>
#include <random>
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
enum BtnInput { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CONFIRM };

class Battle
{
public:
	// Matches pokeemerald's B_OUTCOME_* constants closely enough for
	// ScriptVM's `specialvar VAR, GetBattleOutcome` to report a real result.
	enum Outcome { OUTCOME_NONE = 0, OUTCOME_WON = 1, OUTCOME_LOST = 2,
	               OUTCOME_RAN = 4, OUTCOME_CAUGHT = 7 };

private:
	enum Phase { INACTIVE, MSG, ACTION, MOVE, SWITCH };

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
	struct StatStages { int atk = 0, def = 0, spa = 0, spd = 0, spe = 0, acc = 0, eva = 0; };
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

	void input(BtnInput b);
	void tick(float dt);          // drives the hit shake animation
	void draw(sf::RenderTarget& target);

private:
	float shake_t; int shake_side; int prev_ehp, prev_php;
};
