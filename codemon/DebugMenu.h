#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "BattleData.h"   // Mon, MoveInfo
#include "GameState.h"
#include "UiFrame.h"
#include <algorithm>
#include <string>

// DebugMenu - a developer cheat menu (toggle with H), for testing without
// playing through the whole story: heal, money, badges, HMs, a starter,
// common items, and skipping whatever script/dialog is currently running.
// Same list-driven shape as MultiChoicePrompt, but with a fixed action list
// dispatched by index in the main loop below instead of a caller-supplied
// options vector.
struct DebugMenu {
    enum Action {
        HEAL_TEAM, ADD_MONEY, ALL_BADGES, GIVE_STARTER, TEACH_HMS,
        GIVE_ITEMS, GIVE_XP, SKIP_SCRIPT, CLOSE, COUNT
    };
    bool open_ = false;
    int cursor = 0;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    bool active() const { return open_; }
    void open() { open_ = true; cursor = 0; }
    void close() { open_ = false; }

    static const char* label(int i) {
        switch (i) {
        case HEAL_TEAM:    return "Team komplett heilen";
        case ADD_MONEY:    return "+50.000 Geld";
        case ALL_BADGES:   return "Alle 8 Orden geben";
        case GIVE_STARTER: return "Starter-Pokemon waehlen";
        case TEACH_HMS:    return "Alle Hm-Attacken lehren (Leadmon)";
        case GIVE_ITEMS:   return "99x Poke Ball / Trank / Rare Candy";
        case GIVE_XP:      return "+1000 EP fuer das ganze Team";
        case SKIP_SCRIPT:  return "Laufendes Skript/Dialog abbrechen";
        case CLOSE:        return "Schliessen";
        default: return "";
        }
    }

    // Returns the chosen action, or -1 if just navigating/still open.
    int input(BtnInput b) {
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor + 1 < COUNT) cursor++;
        else if (b == BTN_CONFIRM) { open_ = false; return cursor; }
        return -1;
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        float w = 340.f, h = 30.f + COUNT * 30.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        std::string title_s = "DEBUG-MENUE";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 16);
        title.setPosition(x + 12, y + 6); title.setFillColor(sf::Color(150, 40, 40));
        target.draw(title);
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        for (int i = 0; i < COUNT; ++i) {
            bool sel = i == cursor;
            float ry = y + 34.f + i * 30.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 10, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 16);
                    mark.setPosition(x + 12, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            std::string s = label(i);
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, 15);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};
