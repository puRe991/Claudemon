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
private:
	enum Phase { INACTIVE, MSG, ACTION, MOVE };

	BattleData* data;
	std::mt19937* rng;
	GameState* gs;
	std::vector<Mon>* team;
	std::vector<Mon>* box;
	int action_cursor;
	sf::Font font; bool font_ok;
	UiFrame frame;

	Mon* player;                 // owned by caller; mutated here
	Mon enemy;
	bool is_trainer;
	std::string enemy_title;     // "" for wild, trainer display name otherwise
	std::vector<std::pair<std::string, int>> party;   // remaining trainer mons
	size_t party_idx;

	std::vector<std::string> log;
	size_t log_pos;
	Phase phase;
	Phase after_msg;             // where to go once the log is exhausted (MENU/INACTIVE)
	int cursor;
	bool over, victory;

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
	static std::string nice(const std::string& id);   // POKEMON_NAME -> Pokemon Name

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

	void input(BtnInput b);
	void tick(float dt);          // drives the hit shake animation
	void draw(sf::RenderTarget& target);

private:
	float shake_t; int shake_side; int prev_ehp, prev_php;
};
