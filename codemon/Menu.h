#pragma once
#include <string>
#include <map>
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
	enum Screen { CLOSED, MAIN, BAG, PARTY };
	sf::Font font; bool font_ok;
	Screen screen;
	int cursor;
	GameState* gs;
	Mon* party;
	std::map<std::string, sf::Texture> item_tex;
	sf::Texture mon_tex; bool mon_loaded;

	const sf::Texture* item_icon(const std::string& item);

public:
	Menu();
	void configure(GameState* g, Mon* p);
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	bool active() const { return screen != CLOSED; }
	void open();
	void close();
	void input(BtnInput b);
	void draw(sf::RenderTarget& target);
};
