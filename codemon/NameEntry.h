#pragma once
#include "SFML/Graphics.hpp"
#include "UiFrame.h"
#include <cstdlib>
#include <string>

/******************************************************************************
NameEntry - the on-screen letter-grid naming keyboard real Gen-3 games use
(A-Z plus DEL/OK), reused both for the player's own name and their rival's.
Grid-driven like every other menu in this engine (WASD + confirm), not free
keyboard typing, to stay consistent with the rest of the UI. Self-contained
blocking run(), same shape as TitleScreen/GenderSelect.
*****************************************************************************/
struct NameEntry {
    static constexpr int COLS = 7, ROWS = 4, MAX_LEN = 7;
    // 26 letters + DEL + OK, row-major, filling exactly COLS*ROWS cells.
    static constexpr const char* CELLS[COLS * ROWS] = {
        "A","B","C","D","E","F","G",
        "H","I","J","K","L","M","N",
        "O","P","Q","R","S","T","U",
        "V","W","X","Y","Z","DEL","OK",
    };
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }

    void draw(sf::RenderTarget& w, const std::string& title, const std::string& name, int cur_r, int cur_c) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        if (!font_ok) return;
        sf::Text title_t(sf::String::fromUtf8(title.begin(), title.end()), font, 20);
        sf::FloatRect tb = title_t.getLocalBounds();
        title_t.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.08f);
        title_t.setFillColor(head_col);
        w.draw(title_t);

        // Typed-so-far preview, with a blinking-style underline cursor cell.
        float name_bar_w = 260.f, name_bar_h = 40.f;
        float name_bar_x = size.x / 2.f - name_bar_w / 2.f, name_bar_y = size.y * 0.17f;
        if (frame.ready()) frame.draw(w, name_bar_x, name_bar_y, name_bar_w, name_bar_h, 2.f);
        std::string shown = name.empty() ? "_" : name;
        sf::Text name_t(sf::String::fromUtf8(shown.begin(), shown.end()), font, 22);
        name_t.setPosition(name_bar_x + 16, name_bar_y + 6);
        name_t.setFillColor(body_col);
        w.draw(name_t);

        float grid_w = 420.f, grid_h = 220.f;
        float grid_x = size.x / 2.f - grid_w / 2.f, grid_y = size.y * 0.36f;
        // A light panel behind the grid, same as every other bordered card in
        // this UI -- without it, body_col's dark navy letters are unreadable
        // straight against the dark green screen background.
        if (frame.ready()) frame.draw(w, grid_x, grid_y, grid_w, grid_h, 2.f);
        float cell_w = grid_w / COLS, cell_h = grid_h / ROWS;
        for (int r = 0; r < ROWS; ++r) {
            for (int c = 0; c < COLS; ++c) {
                const char* label = CELLS[r * COLS + c];
                float cx = grid_x + c * cell_w, cy = grid_y + r * cell_h;
                bool sel = r == cur_r && c == cur_c;
                if (sel) {
                    sf::RectangleShape hl(sf::Vector2f(cell_w - 4.f, cell_h - 4.f));
                    hl.setPosition(cx + 2.f, cy + 2.f);
                    hl.setFillColor(sf::Color(24, 72, 160, 90));
                    w.draw(hl);
                }
                sf::Text t(label, font, 16);
                sf::FloatRect lb = t.getLocalBounds();
                t.setPosition(cx + cell_w / 2.f - lb.width / 2.f - lb.left,
                               cy + cell_h / 2.f - lb.height / 2.f - lb.top);
                t.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
                w.draw(t);
            }
        }
        std::string hint_s = "[WASD] wählen  [SPACE] bestätigen  [BACKSPACE] löschen";
        sf::Text hint(sf::String::fromUtf8(hint_s.begin(), hint_s.end()), font, 13);
        sf::FloatRect hb = hint.getLocalBounds();
        hint.setPosition(size.x / 2.f - hb.width / 2.f, size.y - 34);
        hint.setFillColor(sf::Color(230, 230, 230));
        w.draw(hint);
    }

    // Blocks until "OK" is confirmed with a non-empty name. `def` pre-fills
    // the name (real games start blank, but a sane default keeps this
    // skippable-feeling and gives something sensible if the window closes).
    std::string run(sf::RenderWindow& w, const std::string& title, const std::string& def) {
        std::string name = def.substr(0, MAX_LEN);
        int cur_r = 0, cur_c = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::W && cur_r > 0) cur_r--;
                else if (event.key.code == sf::Keyboard::S && cur_r < ROWS - 1) cur_r++;
                else if (event.key.code == sf::Keyboard::A && cur_c > 0) cur_c--;
                else if (event.key.code == sf::Keyboard::D && cur_c < COLS - 1) cur_c++;
                else if (event.key.code == sf::Keyboard::BackSpace) {
                    if (!name.empty()) name.pop_back();
                } else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return) {
                    std::string cell = CELLS[cur_r * COLS + cur_c];
                    if (cell == "DEL") { if (!name.empty()) name.pop_back(); }
                    else if (cell == "OK") { if (!name.empty()) return name; }
                    else if ((int)name.size() < MAX_LEN) name += cell;
                }
            }
            draw(w, title, name, cur_r, cur_c);
            w.display();
        }
        std::exit(0);
    }
};
