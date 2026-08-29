#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput
#include "GameState.h"
#include "ItemDisplay.h"
#include "UiFrame.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

// `pokemart <label>` (the mart buy screen): the VM can't drive a scrolling
// item list + quantity stepper itself (same reason as StarterSelect and
// YesNoPrompt above), so this shows a real shop UI built from the map's
// item list (ScriptVM::shop_items()) and GameState's money/bag, then calls
// ScriptVM::close_shop() once the player leaves.
struct Shop {
    enum Stage { LIST, QTY, MSG };
    Stage stage = LIST;
    bool open_ = false, done_ = false;
    std::vector<std::string> items;
    int cursor = 0, qty = 1;
    GameState* gs = nullptr;
    const std::unordered_map<std::string, int>* prices = nullptr;
    sf::Font font; bool font_ok = false;
    std::string flash;   // transient feedback ("Gekauft!" / "Nicht genug Geld!")
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    std::unordered_map<std::string, sf::Texture> icon_cache;

    bool load() {
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    }
    void configure(GameState* g, const std::unordered_map<std::string, int>* p) {
        this->gs = g; this->prices = p;
    }
    // "ITEM_POTION" -> assets/items/potion.png, cached like Menu::item_icon.
    const sf::Texture* icon_of(const std::string& item) {
        std::string f = item;
        if (f.rfind("ITEM_", 0) == 0) f = f.substr(5);
        for (char& c : f) c = (char)std::tolower((unsigned char)c);
        auto it = icon_cache.find(f);
        if (it != icon_cache.end()) return it->second.getSize().x ? &it->second : nullptr;
        sf::Texture tex;
        if (!tex.loadFromFile("assets/items/" + f + ".png")) { icon_cache[f]; return nullptr; }
        tex.setSmooth(false);
        icon_cache[f] = tex;
        return &icon_cache[f];
    }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }

    void open(const std::vector<std::string>& list) {
        items = list; cursor = 0; qty = 1; stage = LIST;
        open_ = true; done_ = false; flash.clear();
    }

    int price_of(const std::string& item) const {
        if (!prices) return 0;
        auto it = prices->find(item);
        return it == prices->end() ? 0 : it->second;
    }

    void input(BtnInput b) {
        if (stage == MSG) {
            if (b == BTN_CONFIRM) stage = LIST;
            return;
        }
        int n = (int)items.size();   // row `n` is the trailing "Verlassen"
        if (stage == LIST) {
            if (b == BTN_UP && cursor > 0) cursor--;
            else if (b == BTN_DOWN && cursor < n) cursor++;
            else if (b == BTN_CONFIRM) {
                if (cursor == n) { open_ = false; done_ = true; return; }
                int price = price_of(items[cursor]);
                if (price <= 0 || !gs || gs->money < price) {
                    flash = "Nicht genug Geld!"; stage = MSG; return;
                }
                qty = 1; stage = QTY;
            }
        } else if (stage == QTY) {
            int price = price_of(items[cursor]);
            int max_afford = price > 0 && gs ? gs->money / price : 1;
            int cap = std::min(99, std::max(1, max_afford));
            if (b == BTN_UP && qty < cap) qty++;
            else if (b == BTN_DOWN && qty > 1) qty--;
            else if (b == BTN_LEFT) stage = LIST;
            else if (b == BTN_CONFIRM && gs) {
                gs->money -= price * qty;
                gs->give_item(items[cursor], qty);
                flash = "Gekauft: " + item_display_name(items[cursor]) +
                        " x" + std::to_string(qty) + "!";
                stage = MSG;
            }
        }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        sf::RectangleShape dim(size);
        dim.setFillColor(sf::Color(0, 0, 0, 140));
        target.draw(dim);

        const sf::Color head_col(24, 72, 160), body_col(40, 40, 56), muted_col(100, 100, 112);
        auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color col) {
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, cs);
            t.setPosition(px, py); t.setFillColor(col); target.draw(t);
        };

        sf::FloatRect panel(size.x * 0.5f - 200, 20, 400, size.y - 40);
        frame.draw(target, panel.left, panel.top, panel.width, panel.height, 3.f);

        sf::FloatRect money_r(panel.left, panel.top - 6, 180, 40);
        frame.draw(target, money_r.left, money_r.top, money_r.width, money_r.height, 2.f);
        text("Geld: " + std::to_string(gs ? gs->money : 0) + " P",
             money_r.left + 14, money_r.top + 8, 16, body_col);

        if (stage == MSG) {
            text(flash, panel.left + 30, panel.top + panel.height * 0.4f, 18, body_col);
            text("[SPACE] weiter", panel.left + 30, panel.top + panel.height * 0.4f + 34, 14, muted_col);
            target.setView(saved);
            return;
        }

        // rows[0..items.size()-1] are the items, the trailing row is "Verlassen"
        int row_count = (int)items.size() + 1;
        const int visible = 7;
        int scroll = std::max(0, std::min(cursor - visible / 2, row_count - visible));
        scroll = std::max(0, scroll);
        float x = panel.left + 24, y0 = panel.top + 56;
        for (int r = 0; r < visible && scroll + r < row_count; ++r) {
            int i = scroll + r;
            bool sel = i == cursor;
            float ry = y0 + r * 42;
            if (sel) {
                if (cursor_ok) { sf::Sprite cs(cursor_tex); cs.setPosition(x - 22, ry - 2); target.draw(cs); }
                else text(">", x - 18, ry, 18, head_col);
            }
            if (i < (int)items.size()) {
                const sf::Texture* ic = icon_of(items[i]);
                if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
                text(item_display_name(items[i]), x + 36, ry, 17, sel ? head_col : body_col);
                std::string price_s = std::to_string(price_of(items[i])) + " P";
                text(price_s, panel.left + panel.width - 26 - price_s.size() * 9.f, ry, 16,
                     sel ? head_col : muted_col);
            } else {
                text("Verlassen", x + 36, ry, 17, sel ? head_col : body_col);
            }
        }

        if (stage == QTY) {
            sf::FloatRect qr(panel.left + 20, panel.top + panel.height - 84, panel.width - 40, 64);
            frame.draw(target, qr.left, qr.top, qr.width, qr.height, 2.f);
            text("Anzahl: " + std::to_string(qty), qr.left + 16, qr.top + 8, 17, body_col);
            text(std::to_string(price_of(items[cursor]) * qty) + " P",
                 qr.left + 16, qr.top + 32, 16, muted_col);
        }
        std::string hint_s = stage == QTY
            ? "[W/S] Anzahl  [A] zurueck  [SPACE] kaufen"
            : "[W/S] waehlen  [SPACE] auswaehlen";
        text(hint_s, panel.left, panel.top + panel.height + 8, 13, sf::Color(230, 230, 230));
        target.setView(saved);
    }
};
