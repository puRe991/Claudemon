#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "UiFrame.h"

/******************************************************************************
StarterSelect - the one-time "choose your partner" screen (Treecko / Torchic /
Mudkip), matching Prof. Birch's bag on Route 101. Opt-in via
CODEMON_CHOOSE_STARTER so existing demos/tests keep their fixed Treecko start;
real play always opens it. Follows the Menu/Minigame active()/input()/draw()
shape so it slots into both the headless token loop and the interactive one.
*****************************************************************************/
struct StarterSelect {
    static constexpr const char* SPECIES[3] = {"TREECKO", "TORCHIC", "MUDKIP"};
    bool open_ = false, done_ = false;
    int cursor = 1;
    sf::Font font; bool font_ok = false;
    sf::Texture tex[3]; bool tex_ok[3] = {false, false, false};
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        for (int i = 0; i < 3; ++i)
            tex_ok[i] = tex[i].loadFromFile(std::string("assets/pokemon/") + SPECIES[i] + ".png");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void open() { open_ = true; done_ = false; cursor = 1; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }               // caller has consumed the choice
    const char* chosen() const { return SPECIES[cursor]; }

    void input(BtnInput b) {
        if (b == BTN_LEFT && cursor > 0) cursor--;
        else if (b == BTN_RIGHT && cursor < 2) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        sf::RectangleShape bg(size);
        bg.setFillColor(sf::Color(20, 40, 32));
        target.draw(bg);

        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        sf::FloatRect title_r(size.x * 0.5f - 220, 22, 440, 44);
        frame.draw(target, title_r.left, title_r.top, title_r.width, title_r.height, 2.5f);
        std::string title_s = "Wähle dein Partner-Pokémon!";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 20);
        title.setPosition(title_r.left + 20, title_r.top + 12);
        title.setFillColor(body_col);
        target.draw(title);

        // Front sprites are 64x64 native; at the 3x scale used elsewhere for
        // this screen that's 192x192, so the card has to be sized for that
        // (not just for the label under it) or the sprite spills out of it.
        const float card_w = 210.f, card_h = 250.f, sprite_px = 64.f * 3.f;
        for (int i = 0; i < 3; ++i) {
            float cx = size.x * (0.2f + 0.3f * i);
            float card_top = 96.f;
            sf::FloatRect card(cx - card_w / 2.f, card_top, card_w, card_h);
            bool sel = i == cursor;
            frame.draw(target, card.left, card.top, card.width, card.height, 2.f);
            float sx = cx - sprite_px / 2.f, sy = card_top + 14.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(card.left - 22.f, sy + sprite_px * 0.35f);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 22);
                    mark.setPosition(card.left - 18.f, sy + sprite_px * 0.3f);
                    mark.setFillColor(head_col);
                    target.draw(mark);
                }
            }
            if (tex_ok[i]) {
                sf::Sprite s(tex[i]);
                s.setScale(3.f, 3.f);
                s.setPosition(sx, sy);
                target.draw(s);
            }
            sf::Text label(SPECIES[i], font, 16);
            sf::FloatRect lb = label.getLocalBounds();
            label.setPosition(cx - lb.width / 2.f, sy + sprite_px + 6.f);
            label.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
            target.draw(label);
        }

        sf::FloatRect hint_r(size.x * 0.5f - 170, size.y - 56, 340, 40);
        frame.draw(target, hint_r.left, hint_r.top, hint_r.width, hint_r.height, 2.f);
        sf::Text hint("[A/D] waehlen   [SPACE] bestaetigen", font, 14);
        hint.setPosition(hint_r.left + 16, hint_r.top + 12);
        hint.setFillColor(body_col);
        target.draw(hint);
        target.setView(saved);
    }
};
