#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "UiFrame.h"
#include <string>
#include <vector>

// `multichoice`/`multichoicedefault`: a fixed option list (ScriptVM's
// MULTICHOICE_LISTS) the player picks one of, same block-and-resume shape as
// PartyPicker above -- the VM can't drive a real cursor-driven menu itself.
struct MultiChoicePrompt {
    bool open_ = false, done_ = false;
    int cursor = 0;
    std::vector<std::string> options;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void open(const std::vector<std::string>& opts, int default_idx) {
        options = opts; open_ = true; done_ = false;
        cursor = (default_idx >= 0 && default_idx < (int)opts.size()) ? default_idx : 0;
    }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    int chosen() const { return cursor; }

    void input(BtnInput b) {
        int n = (int)options.size();
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor + 1 < n) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        int n = (int)options.size();
        float w = 260.f, h = 30.f + n * 32.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        for (int i = 0; i < n; ++i) {
            bool sel = i == cursor;
            float ry = y + 16.f + i * 32.f;
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
            sf::Text t(sf::String::fromUtf8(options[i].begin(), options[i].end()), font, 16);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};
