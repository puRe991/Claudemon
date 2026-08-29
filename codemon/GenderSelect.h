#pragma once
#include "SFML/Graphics.hpp"
#include "UiFrame.h"
#include <cstdlib>

/******************************************************************************
GenderSelect - the "who are you" screen shown once at the start of a brand
new game: Brendan (boy) or May (girl), same portraits real Emerald uses on
its own character-select screen. Determines the overworld sprite sheet and
`checkplayergender`'s answer. Self-contained blocking run(), same shape as
TitleScreen -- this runs before the main session/window even exist.
*****************************************************************************/
struct GenderSelect {
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    sf::Texture portrait[2]; bool portrait_ok[2] = {false, false};   // 0=Brendan, 1=May

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        portrait_ok[0] = portrait[0].loadFromFile("assets/trainers/brendan.png");
        portrait_ok[1] = portrait[1].loadFromFile("assets/trainers/may.png");
    }

    void draw(sf::RenderTarget& w, int cursor) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        if (font_ok) {
            std::string title_s = "Bist du ein Junge oder ein Mädchen?";
            sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 22);
            sf::FloatRect tb = title.getLocalBounds();
            title.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.18f);
            title.setFillColor(head_col);
            w.draw(title);
        }
        static const char* NAMES[2] = {"JUNGE", "MÄDCHEN"};
        const float card_w = 160.f, card_h = 200.f, sprite_px = 64.f * 2.f;
        for (int i = 0; i < 2; ++i) {
            float cx = size.x * (0.32f + 0.36f * i);
            float card_top = size.y * 0.32f;
            sf::FloatRect card(cx - card_w / 2.f, card_top, card_w, card_h);
            bool sel = i == cursor;
            if (frame.ready()) frame.draw(w, card.left, card.top, card.width, card.height, 2.f);
            float sx = cx - sprite_px / 2.f, sy = card_top + 14.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(card.left - 22.f, sy + sprite_px * 0.35f);
                    w.draw(cs);
                } else if (font_ok) {
                    sf::Text mark(">", font, 22);
                    mark.setPosition(card.left - 18.f, sy + sprite_px * 0.3f);
                    mark.setFillColor(head_col);
                    w.draw(mark);
                }
            }
            if (portrait_ok[i]) {
                sf::Sprite s(portrait[i]);
                s.setScale(2.f, 2.f);
                s.setPosition(sx, sy);
                w.draw(s);
            }
            if (font_ok) {
                std::string name_s = NAMES[i];
                sf::Text label(sf::String::fromUtf8(name_s.begin(), name_s.end()), font, 16);
                sf::FloatRect lb = label.getLocalBounds();
                label.setPosition(cx - lb.width / 2.f, sy + sprite_px + 10.f);
                label.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
                w.draw(label);
            }
        }
        if (font_ok) {
            std::string hint_s = "[A/D] wählen   [SPACE] bestätigen";
            sf::Text hint(sf::String::fromUtf8(hint_s.begin(), hint_s.end()), font, 14);
            sf::FloatRect hb = hint.getLocalBounds();
            hint.setPosition(size.x / 2.f - hb.width / 2.f, size.y - 40);
            hint.setFillColor(sf::Color(230, 230, 230));
            w.draw(hint);
        }
    }

    // Blocks until the player picks Brendan (false) or May (true).
    bool run(sf::RenderWindow& w) {
        int cursor = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::A && cursor > 0) cursor--;
                else if (event.key.code == sf::Keyboard::D && cursor < 1) cursor++;
                else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    return cursor == 1;
            }
            draw(w, cursor);
            w.display();
        }
        std::exit(0);
    }
};
