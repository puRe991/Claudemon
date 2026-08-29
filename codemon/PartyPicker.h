#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "BattleData.h"   // Mon
#include "ItemDisplay.h"
#include "UiFrame.h"
#include <string>
#include <vector>

// `special ChoosePartyMon` (in-game trades: Rustboro/Fortree/Pacifidlog/
// Battle Frontier Lounge NPCs offering to swap a mon): same story as
// StarterSelect/YesNoPrompt above -- the VM can't drive a real party list
// itself, so this shows one (plus a trailing "Abbrechen" row) and calls
// ScriptVM::resolve_choose_party_mon() with the picked index, or -1.
struct PartyPicker {
    bool open_ = false, done_ = false;
    int cursor = 0;
    std::vector<Mon>* team = nullptr;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void configure(std::vector<Mon>* t) { team = t; }
    void open() { open_ = true; done_ = false; cursor = 0; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    int chosen() const {
        int n = team ? (int)team->size() : 0;
        return cursor < n ? cursor : -1;   // last row ("Abbrechen") -> -1
    }

    void input(BtnInput b) {
        int rows = (team ? (int)team->size() : 0) + 1;
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor < rows - 1) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        int n_mons = team ? (int)team->size() : 0;
        float w = 260.f, h = 50.f + (n_mons + 1) * 32.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56), sel_col(24, 72, 160);
        std::string title_s = "Waehle ein POKéMON";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 18);
        title.setPosition(x + 14, y + 10);
        title.setFillColor(head_col);
        target.draw(title);
        auto row = [&](int i, const std::string& label) {
            bool sel = i == cursor;
            float ry = y + 46.f + i * 32.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 10, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 18);
                    mark.setPosition(x + 12, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(label, font, 16);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        };
        for (int i = 0; i < n_mons; ++i)
            row(i, item_display_name((*team)[i].species) + "  Lv" + std::to_string((*team)[i].level));
        row(n_mons, "Abbrechen");
        target.setView(saved);
    }
};
