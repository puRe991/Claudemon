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
	enum Screen { CLOSED, MAIN, BAG, PARTY, PC, POKENAV, TEACH };
	sf::Font font; bool font_ok;
	Screen screen;
	int cursor;
	int bag_cursor;         // selected row in the BAG list
	int teach_cursor;       // selected party member when teaching a TM/HM
	std::string teach_item; // ITEM_TMxx / ITEM_HMxx being taught
	std::string teach_move; // the move that TM/HM teaches
	std::string flash;      // transient status line (e.g. "X learned MOVE!")
	GameState* gs;
	BattleData* bdata;
	std::vector<Mon>* team;
	std::vector<Mon>* box;
	std::string location;

	// Sorted snapshot of the bag (unordered_map has no stable order for a cursor).
	std::vector<std::pair<std::string, int>> bag_sorted() const;
	// Teach the pending TM/HM move to the selected party member.
	void teach_selected();
	std::map<std::string, sf::Texture> item_tex;
	std::map<std::string, sf::Texture> mon_tex;
	std::map<std::string, sf::Texture> type_tex;

	const sf::Texture* item_icon(const std::string& item);
	const sf::Texture* mon_icon(const std::string& species);
	const sf::Texture* type_icon(const std::string& type);

public:
	Menu();
	void configure(GameState* g, std::vector<Mon>* team, std::vector<Mon>* box,
	               BattleData* bd = nullptr);
	void set_location(const std::string& loc) { this->location = loc; }
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	bool active() const { return screen != CLOSED; }
	void open();
	void close();
	void input(BtnInput b);
	void draw(sf::RenderTarget& target);
};
