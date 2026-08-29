#pragma once
#include "SFML/Graphics.hpp"
#include "DialogBox.h"
#include <cstdlib>

/******************************************************************************
EarlyAccessNotice - a one-shot disclaimer shown right after character
creation on a brand new game, using the same DialogBox the overworld uses for
NPC text so it looks consistent with the rest of the UI. Self-contained
blocking run(), same shape as TitleScreen/GenderSelect/NameEntry.
*****************************************************************************/
struct EarlyAccessNotice {
    void run(sf::RenderWindow& w) {
        DialogBox box;
        box.load_font();
        box.open("", "Dies ist eine Early-Access-Version.\x1f"
                      "Es können noch verschiedene Bugs auftreten, "
                      "und nicht alles ist bereits perfekt dem Original nachgestellt.");
        while (w.isOpen() && box.is_active()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    box.advance();
            }
            w.clear(sf::Color(24, 60, 40));
            box.draw(w);
            w.display();
        }
    }
};
