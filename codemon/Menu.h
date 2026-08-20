#pragma once
#include <string>
#include <map>
#include <vector>
#include "SFML/Graphics.hpp"
#include "GameState.h"
#include "BattleData.h"   // Mon
#include "Battle.h"       // BtnInput

/******************************************************************************
Menu - the overworld start menu with a Bag (item icons + counts) and a Pokemon
(party) screen. Opened with M; drawn in screen space over the world.
*****************************************************************************/
class Menu
{
private:
	enum Screen { CLOSED, MAIN, BAG, PARTY, PC };
	sf::Font font; bool font_ok;
	Screen screen;
	int cursor;
	GameState* gs;
	std::vector<Mon>* team;
	std::vector<Mon>* box;
	std::map<std::string, sf::Texture> item_tex;
	std::map<std::string, sf::Texture> mon_tex;

	const sf::Texture* item_icon(const std::string& item);
	const sf::Texture* mon_icon(const std::string& species);

public:
	Menu();
	void configure(GameState* g, std::vector<Mon>* team, std::vector<Mon>* box);
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	bool active() const { return screen != CLOSED; }
	void open();
	void close();
	void input(BtnInput b);
	void draw(sf::RenderTarget& target);
};
