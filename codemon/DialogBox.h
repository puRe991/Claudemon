#pragma once
#include <string>
#include "SFML/Graphics.hpp"

/******************************************************************************
DialogBox - a bottom text box for NPC dialog.

Drawn in screen/pixel space (using the target's default view) so it stays put
regardless of the world camera. Text is word-wrapped to the box width. Call
open() with a speaker + line, is_active() to gate input/movement, and close()
to dismiss.
*****************************************************************************/
class DialogBox
{
private:
	sf::Font font;
	bool font_ok;
	bool active;
	std::string speaker;
	std::string text;

	std::string wrap(const std::string& s, unsigned int px_width,
	                 unsigned int char_size) const;

public:
	DialogBox();
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	void open(const std::string& who, const std::string& line);
	void close();
	bool is_active() const;

	// Draw over the current frame in pixel coordinates.
	void draw(sf::RenderTarget& target);
};
