#pragma once
#include <string>
#include <vector>
#include "SFML/Graphics.hpp"
#include "UiFrame.h"

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
	std::vector<std::string> pages;   // one entry per dialog page
	size_t page;

	std::string wrap(const std::string& s, unsigned int px_width,
	                 unsigned int char_size) const;
	UiFrame frame;

public:
	DialogBox();
	bool load_font(const std::string& path = "assets/fonts/DejaVuSans.ttf");

	// `line` may contain U+001F separators to split it into pages.
	void open(const std::string& who, const std::string& line);
	// Advance to the next page; closes the box after the last one.
	void advance();
	void close();
	bool is_active() const;

	// Draw over the current frame in pixel coordinates.
	void draw(sf::RenderTarget& target);
};
