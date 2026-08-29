#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "UiFrame.h"
#include <sstream>
#include <string>
#include <vector>

// `msgbox ..., MSGBOX_YESNO` (heal at the Pokemon Center, buy/sell
// confirmations, ...): the VM can't drive a cursor-driven choice itself
// (same reason as StarterSelect above), so this shows a real Ja/Nein
// prompt and the pick is fed back via ScriptVM::resolve_yesno().
struct YesNoPrompt {
    bool open_ = false, done_ = false;
    int cursor = 0;   // 0 = Ja, 1 = Nein
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    // Set only by callers that aren't already showing their own question
    // text via the message box first (VM-driven msgboxyesno flows do that
    // via `box`, so leave this empty and get the plain corner prompt as
    // before; the Surf gate below has no such preceding message, so it
    // passes its own question straight into the prompt).
    std::string prompt;

    bool load() {
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    }
    void open(const std::string& p = "") { open_ = true; done_ = false; cursor = 0; prompt = p; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    bool yes() const { return cursor == 0; }

    void input(BtnInput b) {
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor < 1) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        float w = 132.f, h = 78.f;
        float x = size.x - w - 14.f, y = size.y - h - 100.f;
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        if (!prompt.empty()) {
            // Crude greedy word-wrap (no per-glyph measurement, just a
            // char-count budget tuned for this font/size/box width) -- the
            // only prompt text this widget ever shows is the short, fixed
            // Surf question, so this doesn't need to be a general wrapper.
            std::vector<std::string> plines;
            std::string cur;
            std::stringstream ss(prompt);
            std::string word;
            while (ss >> word) {
                if (!cur.empty() && cur.size() + 1 + word.size() > 38) {
                    plines.push_back(cur); cur.clear();
                }
                if (!cur.empty()) cur += ' ';
                cur += word;
            }
            if (!cur.empty()) plines.push_back(cur);
            float pw = 340.f, ph = 34.f + plines.size() * 22.f;
            float px = size.x - pw - 14.f, py = y - ph - 8.f;
            frame.draw(target, px, py, pw, ph, 2.5f);
            for (size_t li = 0; li < plines.size(); ++li) {
                sf::Text t(sf::String::fromUtf8(plines[li].begin(), plines[li].end()), font, 15);
                t.setPosition(px + 14, py + 14 + li * 22.f);
                t.setFillColor(body_col);
                target.draw(t);
            }
        }
        frame.draw(target, x, y, w, h, 2.5f);
        const char* opts[2] = {"Ja", "Nein"};
        for (int i = 0; i < 2; ++i) {
            bool sel = i == cursor;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 12, y + 12 + i * 32);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 20);
                    mark.setPosition(x + 14, y + 10 + i * 32);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(opts[i], font, 20);
            t.setPosition(x + 40, y + 10 + i * 32);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};
