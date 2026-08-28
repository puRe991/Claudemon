#pragma once
#include "SFML/Graphics.hpp"
#include "MenuModel.h"
#include <string>

/******************************************************************************
MainMenu - a Pokemon-style title screen and main menu.

Screens:
  Title   -> big game title with a blinking "PRESS ENTER" prompt
  Menu    -> New Game / Continue / Options / Quit in a bordered box
  Options -> a controls / info screen

Everything is drawn against a fixed virtual resolution (passed to the ctor);
scaling to the real window is handled by the caller via an SFML view.
******************************************************************************/

// What the menu is asking the surrounding game loop to do.
enum class MenuResult {
	None,
	StartGame,
	Quit
};

class MainMenu
{
public:
	MainMenu(float virtual_w, float virtual_h);

	//Load the font used for all menu text. Returns false on failure.
	bool load(const std::string& font_path);

	//Feed a key press. Returns StartGame/Quit when the user leaves the menu.
	MenuResult handle_key(sf::Keyboard::Key key);

	//Advance animations (blinking prompt, cursor bob).
	void update(float dt);

	//Draw the current screen against the virtual resolution.
	void render(sf::RenderTarget& target);

private:
	enum class Screen { Title, Menu, Options };

	float     m_vw;
	float     m_vh;
	sf::Font  m_font;
	bool      m_loaded;
	Screen    m_screen;
	MenuModel m_menu;
	float     m_time; // seconds accumulated, drives animations

	/* rendering helpers */
	void draw_background(sf::RenderTarget& target);
	void draw_text(sf::RenderTarget& target, const std::string& s, unsigned int size,
	               float cx, float cy, sf::Color color, bool shadow = false);
	void draw_title_screen(sf::RenderTarget& target);
	void draw_menu_screen(sf::RenderTarget& target);
	void draw_options_screen(sf::RenderTarget& target);
};
