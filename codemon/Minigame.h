#pragma once
#include <string>
#include <random>
#include "SFML/Graphics.hpp"
#include "GameState.h"
#include "Battle.h"   // BtnInput

/******************************************************************************
Minigame - the Game Corner / activity games, played for COINS (stored in
GameState). A small select menu launches Slot Machine and Roulette (turn based)
and Berry Blender and Pokemon Jump (timing based). Drawn in screen space.
*****************************************************************************/
class Minigame
{
private:
	enum Game { NONE, SELECT, SLOT, ROULETTE, BLENDER, JUMP };
	Game game;
	int cursor;
	sf::Font font; bool font_ok;
	GameState* gs;
	std::mt19937* rng;
	std::string msg;

	// slot machine
	int reel[3]; bool spin[3]; int stopped;

	// roulette
	int bet_choice; int wheel; float wheel_t; bool wheel_spin;

	// berry blender (timing)
	float marker; int blend_score; int blend_presses;

	// pokemon jump (timing)
	float rope_x; float jump_y; float jump_v; int jump_score; bool jump_over;

	int coins() const;
	void add_coins(int n);
	void reset_slot(); void reset_roulette(); void reset_blender(); void reset_jump();

public:
	Minigame();
	void configure(GameState* g, std::mt19937* r);
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	bool active() const { return game != NONE; }
	void open();                 // to the game-select menu
	void close();
	void input(BtnInput b);
	void tick(float dt);
	void draw(sf::RenderTarget& target);
};
