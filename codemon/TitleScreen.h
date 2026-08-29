#pragma once
#include "SFML/Graphics.hpp"
#include "UiFrame.h"
#include <cstdlib>
#include <string>
#include <vector>

// The game's own start screen, shown once at launch before any map loads
// (real interactive play only -- headless tests and CODEMON_MAP demos skip
// straight past it, same as every other test-only branch in main()). No
// licensed logo art exists to import (assets/graphics/intro/ has the full
// animated intro cutscene, but no actual title wordmark), so this is text
// over a plain background, styled like every other screen in this engine
// (UiFrame panel, arrow cursor, same W/S/Space input as the rest of the UI).
struct TitleScreen {
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }

    // Shared by run() below and the headless screenshot harness (which has
    // no real window/event loop to drive run() with, but still wants to
    // render a frame of this to verify the layout).
    void draw(sf::RenderTarget& w, const std::vector<std::string>& opts, int cursor) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        if (font_ok) {
            std::string title_s = "CODEMON";
            sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 64);
            title.setStyle(sf::Text::Bold);
            sf::FloatRect tb = title.getLocalBounds();
            title.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.20f);
            title.setFillColor(sf::Color(255, 232, 160));
            title.setOutlineColor(sf::Color(60, 30, 10));
            title.setOutlineThickness(3.f);
            w.draw(title);

            std::string sub_s = "Ein Pokémon-Fanprojekt";
            sf::Text sub(sf::String::fromUtf8(sub_s.begin(), sub_s.end()), font, 18);
            sf::FloatRect sb = sub.getLocalBounds();
            sub.setPosition(size.x / 2.f - sb.width / 2.f - sb.left, size.y * 0.20f + 78.f);
            sub.setFillColor(sf::Color(230, 230, 230));
            w.draw(sub);
        }
        float bw = 220.f, bh = 24.f + opts.size() * 38.f;
        float bx = size.x / 2.f - bw / 2.f, by = size.y * 0.6f;
        if (frame.ready()) frame.draw(w, bx, by, bw, bh, 2.5f);
        if (font_ok) {
            for (int i = 0; i < (int)opts.size(); ++i) {
                bool sel = i == cursor;
                float ry = by + 16.f + i * 38.f;
                if (sel) {
                    if (cursor_ok) {
                        sf::Sprite cs(cursor_tex);
                        cs.setPosition(bx + 16, ry - 2);
                        w.draw(cs);
                    } else {
                        sf::Text mark(">", font, 18);
                        mark.setPosition(bx + 18, ry - 2);
                        mark.setFillColor(sf::Color(24, 72, 160));
                        w.draw(mark);
                    }
                }
                sf::Text t(sf::String::fromUtf8(opts[i].begin(), opts[i].end()), font, 18);
                t.setPosition(bx + 44, ry);
                t.setFillColor(sel ? sf::Color(24, 72, 160) : sf::Color(40, 40, 56));
                w.draw(t);
            }
        }
    }

    // Blocks until the player picks an option. Returns true for "FORTSETZEN"
    // (only offered when has_save), false for "NEUES SPIEL". Closing the
    // window here exits the whole program, same as closing it mid-game would.
    bool run(sf::RenderWindow& w, bool has_save) {
        std::vector<std::string> opts;
        if (has_save) opts.push_back("FORTSETZEN");
        opts.push_back("NEUES SPIEL");
        int cursor = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::W && cursor > 0) cursor--;
                else if (event.key.code == sf::Keyboard::S && cursor + 1 < (int)opts.size()) cursor++;
                else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    return has_save ? (cursor == 0) : false;
            }
            draw(w, opts, cursor);
            w.display();
        }
        std::exit(0);
    }
};
