#pragma once
#include <string>
#include <vector>
#include <map>
#include <random>
#include "SFML/Graphics.hpp"
#include "BattleData.h"
#include "GameState.h"
#include "UiFrame.h"

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
	bool roll_accuracy(int accuracy) const;
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
