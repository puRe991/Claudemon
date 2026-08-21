#pragma once
#include "SFML/Graphics.hpp"

/******************************************************************************
UiFrame - pokeemerald's authentic 9-slice window border (assets/graphics/
text_window/1.png: a 24x24 sheet, a 3x3 grid of 8x8 tiles - 4 corners, 4
edges, 1 fill), stretched to cover any target rectangle. This is the exact
frame the real game draws behind every standard text box and list menu.
*****************************************************************************/
class UiFrame
{
private:
	sf::Texture tex;
	bool ok;

public:
	UiFrame() : ok(false) {}

	bool load(const std::string& path = "assets/graphics/text_window/1.png") {
		this->ok = this->tex.loadFromFile(path);
		this->tex.setSmooth(false);
		return this->ok;
	}
	bool ready() const { return this->ok; }

	// Draw the frame so its outer edge covers [x,y,w,h] in the target's
	// current coordinate space. `scale` multiplies the native 8px tile size.
	void draw(sf::RenderTarget& target, float x, float y, float w, float h,
	         float scale = 1.f) const {
		if (!this->ok) return;
		const float t = 8.f * scale;
		auto piece = [&](int sx, int sy, float dx, float dy, float dw, float dh) {
			if (dw <= 0.f || dh <= 0.f) return;
			sf::Sprite s(this->tex);
			s.setTextureRect(sf::IntRect(sx, sy, 8, 8));
			s.setPosition(dx, dy);
			s.setScale(dw / 8.f, dh / 8.f);
			target.draw(s);
		};
		float iw = w - 2.f * t, ih = h - 2.f * t;
		// corners
		piece(0, 0,   x,         y,         t, t);
		piece(16, 0,  x + w - t, y,         t, t);
		piece(0, 16,  x,         y + h - t, t, t);
		piece(16, 16, x + w - t, y + h - t, t, t);
		// edges
		piece(8, 0,   x + t,     y,         iw, t);
		piece(8, 16,  x + t,     y + h - t, iw, t);
		piece(0, 8,   x,         y + t,     t, ih);
		piece(16, 8,  x + w - t, y + t,     t, ih);
		// fill
		piece(8, 8,   x + t,     y + t,     iw, ih);
	}
};
