#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Audio.h"
#include "DialogBox.h"
#include "Battle.h"
#include "BattleData.h"
#include "GameState.h"
#include "ScriptVM.h"
#include "Menu.h"
#include "Minigame.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static const int SCALE = 3;
static const float NPC_TICK = 0.45f;
// Fixed camera viewport in tiles (so the window size is independent of the map).
static const int VIEW_TW = 16, VIEW_TH = 12;
static const char* PLAYER_SHEET = "assets/overworld/people_brendan_walking.png";

// "maps/LittlerootTown.map" -> "Littleroot Town", "Route102" -> "Route 102".
static std::string pretty_map(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string s = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = s.rfind(".map");
    if (dot != std::string::npos) s = s.substr(0, dot);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '_') { out += ' '; continue; }
        bool up = std::isupper((unsigned char)c), dg = std::isdigit((unsigned char)c);
        if (i > 0 && (up || (dg && !std::isdigit((unsigned char)s[i - 1]))) &&
            out.size() && out.back() != ' ')
            out += ' ';
        out += c;
    }
    return out;
}

// Draw a map-name banner at the top; alpha fades with `t` (seconds remaining).
static void draw_banner(sf::RenderTarget& target, const sf::Font& font,
                        const std::string& name, float t) {
    if (t <= 0.f || name.empty()) return;
    sf::View saved = target.getView();
    target.setView(target.getDefaultView());
    sf::Vector2f size = target.getView().getSize();
    sf::Uint8 a = (sf::Uint8)(std::min(1.f, t) * 235);
    sf::RectangleShape box(sf::Vector2f(280, 46));
    box.setPosition(18, 14);
    box.setFillColor(sf::Color(20, 28, 48, a));
    box.setOutlineColor(sf::Color(245, 245, 245, a)); box.setOutlineThickness(2.f);
    target.draw(box);
    sf::Text txt(sf::String::fromUtf8(name.begin(), name.end()), font, 22);
    txt.setPosition(32, 24); txt.setFillColor(sf::Color(255, 255, 255, a));
    target.draw(txt);
    target.setView(saved);
}

static DIR convert_key_event(const sf::Event& e) {
    switch (e.key.code) {
    case sf::Keyboard::W: return DIR::N;
    case sf::Keyboard::S: return DIR::S;
    case sf::Keyboard::A: return DIR::W;
    case sf::Keyboard::D: return DIR::E;
    default:              return DIR::NONE;
    }
}

struct Agent {
    Character* ch;
    MoveKind kind;
    DIR pace_dir;
    int home_x, home_y;
    std::string sheet;    // for deriving a speaker name
    std::string dialog;
};

// "people_old_man" / "people_brendan_walking" -> "Old Man" / "Brendan"
static std::string speaker_name(const std::string& sheet) {
    std::string s = sheet;
    size_t us = s.find('_');
    if (us != std::string::npos) s = s.substr(us + 1);          // drop group
    const std::string suffix = "_walking";
    if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(),
        suffix.size(), suffix) == 0)
        s = s.substr(0, s.size() - suffix.size());
    std::string out;
    bool cap = true;
    for (char c : s) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
        else out += c;
    }
    return out;
}

static DIR opposite(DIR d) {
    switch (d) { case DIR::N: return DIR::S; case DIR::S: return DIR::N;
                 case DIR::E: return DIR::W; case DIR::W: return DIR::E;
                 default: return DIR::S; }
}

// Everything tied to the currently-loaded map.
struct Session {
    std::string path;
    Map* map = nullptr;
    Character* player = nullptr;
    std::vector<Agent> agents;
    std::vector<Character*> npc_chars;
    std::vector<Character*> actors;   // npcs + player, for draw/collision
};

static bool actor_at(const std::vector<Character*>& actors, Character* self,
                     int tx, int ty) {
    for (Character* a : actors) {
        if (a == self) continue;
        if (a->get_tile_x() == tx && a->get_tile_y() == ty) return true;
    }
    return false;
}

static void free_session(Session* s) {
    if (!s) return;
    for (Character* n : s->npc_chars) delete n;
    delete s->player;
    delete s->map;
    delete s;
}

// Load a map and build the player + NPC agents. arrival<0 means use the map's
// own start position.
static Session* load_session(const std::string& path, int arr_x, int arr_y,
                             GameState* gs) {
    Session* s = new Session();
    s->path = path;
    s->map = new Map(path);
    int px = (arr_x >= 0) ? arr_x : (int)s->map->get_start_pos().get_x();
    int py = (arr_y >= 0) ? arr_y : (int)s->map->get_start_pos().get_y();

    s->player = new Character(px, py);
    s->player->load_sprite_sheet(PLAYER_SHEET);
    s->player->face(DIR::S);

    for (const NpcSpawn& sp : s->map->npcs()) {
        // pokeemerald FLAG_HIDE_*: this object doesn't exist yet/anymore
        // (not met, already given away, story hasn't reached it, ...).
        if (!sp.hide_flag.empty() && gs && gs->flag(sp.hide_flag)) continue;
        Character* ch = new Character(sp.x, sp.y);
        if (!ch->load_sprite_sheet("assets/overworld/" + sp.sheet + ".png")) {
            delete ch; continue;
        }
        ch->face(sp.facing);
        Agent ag; ag.ch = ch; ag.kind = sp.movement;
        ag.home_x = sp.x; ag.home_y = sp.y;
        ag.pace_dir = (sp.movement == MOVE_PACE_H) ? DIR::E : DIR::N;
        ag.sheet = sp.sheet; ag.dialog = sp.dialog;
        s->agents.push_back(ag);
        s->npc_chars.push_back(ch);
    }
    for (Character* n : s->npc_chars) s->actors.push_back(n);
    s->actors.push_back(s->player);
    return s;
}

static bool try_step(Agent& ag, DIR dir, Map& map, std::vector<Character*>& actors) {
    int tx, ty;
    ag.ch->set_facing(dir);
    ag.ch->target_tile(dir, tx, ty);
    if (map.passable(tx, ty) && !actor_at(actors, ag.ch, tx, ty)) {
        ag.ch->step(dir); return true;
    }
    ag.ch->face(dir); return false;
}

static void tick_npcs(Session* s, std::mt19937& rng) {
    static const DIR dirs[4] = {DIR::N, DIR::S, DIR::E, DIR::W};
    for (Agent& ag : s->agents) {
        switch (ag.kind) {
        case MOVE_WANDER: {
            DIR d = dirs[rng() % 4];
            int tx, ty; ag.ch->target_tile(d, tx, ty);
            if (std::abs(tx - ag.home_x) <= 2 && std::abs(ty - ag.home_y) <= 2)
                try_step(ag, d, *s->map, s->actors);
            else ag.ch->face(d);
            break;
        }
        case MOVE_PACE_V:
            if (!try_step(ag, ag.pace_dir, *s->map, s->actors))
                { ag.pace_dir = (ag.pace_dir == DIR::N) ? DIR::S : DIR::N;
                  try_step(ag, ag.pace_dir, *s->map, s->actors); }
            break;
        case MOVE_PACE_H:
            if (!try_step(ag, ag.pace_dir, *s->map, s->actors))
                { ag.pace_dir = (ag.pace_dir == DIR::E) ? DIR::W : DIR::E;
                  try_step(ag, ag.pace_dir, *s->map, s->actors); }
            break;
        default: break;
        }
    }
}

// Move the player one tile if possible; then, if the destination tile is a
// warp, load the target map and place the player at the arrival warp. Returns
// the (possibly new) session.
static Session* player_step(Session* s, DIR dir, Audio* audio, GameState* gs) {
    int tx, ty;
    s->player->target_tile(dir, tx, ty);
    // Warp/door tiles are impassable metatiles but can be walked onto: the warp
    // overrides collision (that is how doors work in pokeemerald).
    const Warp* target_warp = s->map->warp_at(tx, ty);
    bool blocked = (!s->map->passable(tx, ty) && !target_warp) ||
                   actor_at(s->actors, s->player, tx, ty);
    if (blocked) {
        s->player->face(dir);
        if (audio) audio->play_bump();
        return s;
    }
    s->player->step(dir);
    if (audio) audio->play_step();

    const Warp* wp = s->map->warp_at(s->player->get_tile_x(), s->player->get_tile_y());
    if (wp && wp->dest != "-") {
        Session* ns = load_session("maps/" + wp->dest + ".map", -1, -1, gs);
        if (ns->map->ready()) {
            const Warp* dw = ns->map->warp_by_index(wp->dest_warp);
            if (dw) ns->player->set_tile(dw->x, dw->y);
            free_session(s);
            return ns;
        }
        free_session(ns);   // destination not available; stay put
    }
    return s;
}

// Talk to whatever the player is facing. If a dialog is already open, this
// advances/closes it. Returns true if something happened.
// Find and talk to whichever NPC occupies (tx,ty); returns true if one did.
static bool talk_to_npc_at(Session* s, DialogBox& box, Audio* audio, ScriptVM& vm,
                           int tx, int ty) {
    for (size_t i = 0; i < s->agents.size(); ++i) {
        Agent& ag = s->agents[i];
        if (ag.ch->get_tile_x() == tx && ag.ch->get_tile_y() == ty) {
            ag.ch->face(opposite(s->player->get_facing()));   // turn to the player
            if (audio) audio->play_select();
            std::string label = s->map->npc_script((int)i);
            if (!label.empty() && s->map->has_script(label)) {
                vm.start(label, ag.ch);                        // run its event script
            } else {
                box.open(speaker_name(ag.sheet),
                         ag.dialog.empty() ? "..." : ag.dialog);
            }
            return true;
        }
    }
    return false;
}

static bool interact(Session* s, DialogBox& box, Audio* audio, ScriptVM& vm) {
    if (box.is_active()) { box.advance(); return true; }   // next page / dismiss
    int tx, ty;
    DIR facing = s->player->get_facing();
    s->player->target_tile(facing, tx, ty);
    // 1) an NPC on the faced tile
    if (talk_to_npc_at(s, box, audio, vm, tx, ty)) return true;
    // 1b) a shop/PC counter: the clerk/nurse stands one tile past it, out
    // of normal reach since the counter itself blocks movement (pokeemerald
    // looks through counters the same way -- MetatileBehavior_IsCounter in
    // field_control_avatar.c).
    if (s->map->is_counter(tx, ty)) {
        int cx, cy;
        Character tmp(tx, ty);
        tmp.target_tile(facing, cx, cy);
        if (talk_to_npc_at(s, box, audio, vm, cx, cy)) return true;
    }
    // 2) a readable sign on the faced tile
    const Sign* sg = s->map->sign_at(tx, ty);
    if (sg) {
        box.open("", sg->text);
        if (audio) audio->play_select();
        return true;
    }
    return false;
}

// pokeemerald's MAP_SCRIPT_ON_FRAME_TABLE: on entering a map, run any
// var-gated one-time setup script (e.g. Route 101 advancing
// VAR_ROUTE101_STATE 0->1, which the Birch-rescue coord trigger waits on).
// These are simple setflag/setvar scripts with no blocking ops, so running
// them inline to completion is safe.
static void run_load_triggers(Map* map, GameState& gs, ScriptVM& vm) {
    for (const LoadTrigger& t : map->on_load_triggers()) {
        if (gs.get_var(t.var) == std::atoi(t.val.c_str()))
            vm.start(t.label, nullptr);
    }
}

// After a real step, run a coord_event trigger if the player is on one and its
// variable condition matches.
static void check_trigger(Session* s, ScriptVM& vm, GameState& gs) {
    if (vm.running()) return;
    const ScriptTrigger* t = s->map->trigger_at(s->player->get_tile_x(),
                                                s->player->get_tile_y());
    if (!t || !s->map->has_script(t->label)) return;
    int want = (!t->val.empty() && (std::isdigit((unsigned char)t->val[0])))
                   ? std::atoi(t->val.c_str()) : gs.get_var(t->val);
    if (gs.get_var(t->var) == want) vm.start(t->label, nullptr);
}

// After a real step, maybe start a wild encounter if standing in tall grass.
static void try_encounter(Session* s, Battle& battle, Mon& party,
                          std::mt19937& rng, bool force) {
    if (std::getenv("CODEMON_NO_WILD")) return;    // for scripted trainer demos
    int px = s->player->get_tile_x(), py = s->player->get_tile_y();
    if (!s->map->encounter_here(px, py)) return;
    if (!force && (rng() % 100) >= 22) return;                 // ~22% per step
    std::string sp; int level;
    if (s->map->roll_encounter(rng, sp, level))                // real species + level
        battle.start_wild(sp, level, &party);
}

// A camera that follows the player, clamped to the map bounds.
static sf::View camera_for(Session* s) {
    int tp = s->map->get_tile_size();
    float vw = (float)(VIEW_TW * tp), vh = (float)(VIEW_TH * tp);
    float worldw = (float)(s->map->get_width() * tp);
    float worldh = (float)(s->map->get_height() * tp);
    float cx = s->player->get_tile_x() * tp + tp * 0.5f;
    float cy = s->player->get_tile_y() * tp + tp * 0.5f;
    // clamp so we never show outside the map (unless the map is smaller than view)
    if (worldw >= vw) cx = std::max(vw / 2, std::min(cx, worldw - vw / 2));
    else              cx = worldw / 2;
    if (worldh >= vh) cy = std::max(vh / 2, std::min(cy, worldh - vh / 2));
    else              cy = worldh / 2;
    sf::View v(sf::FloatRect(0, 0, vw, vh));
    v.setCenter(cx, cy);
    return v;
}

static void draw_scene(sf::RenderTarget& target, Session* s) {
    target.setView(camera_for(s));
    s->map->render_to(target);
    std::sort(s->actors.begin(), s->actors.end(),
              [](Character* a, Character* b) { return a->get_tile_y() < b->get_tile_y(); });
    for (Character* a : s->actors) {
        a->update_sprite(s->map->get_tile_size());
        target.draw(*a->get_current_sprite());
    }
}

// Parse "N,N,W,T" into action tokens (N/S/E/W move, T talk) for demo walks.
static std::vector<char> parse_walk(const char* env) {
    std::vector<char> out;
    if (!env) return out;
    std::stringstream ss(env); std::string t;
    while (std::getline(ss, t, ',')) {
        if (t.size() == 1 && std::string("NSEWTMG").find(t[0]) != std::string::npos)
            out.push_back(t[0]);
    }
    return out;
}

static DIR char_to_dir(char c) {
    switch (c) { case 'N': return DIR::N; case 'S': return DIR::S;
                 case 'E': return DIR::E; case 'W': return DIR::W;
                 default: return DIR::NONE; }
}

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

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        for (int i = 0; i < 3; ++i)
            tex_ok[i] = tex[i].loadFromFile(std::string("assets/pokemon/") + SPECIES[i] + ".png");
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
        bg.setFillColor(sf::Color(20, 28, 48, 250));
        target.draw(bg);

        std::string title_s = "Wähle dein Partner-Pokémon!";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 22);
        title.setPosition(size.x * 0.5f - 200, 30);
        title.setFillColor(sf::Color(150, 210, 255));
        target.draw(title);

        for (int i = 0; i < 3; ++i) {
            float x = size.x * (0.18f + 0.32f * i);
            float y = size.y * 0.35f;
            bool sel = i == cursor;
            if (sel) {
                sf::RectangleShape hl(sf::Vector2f(110, 110));
                hl.setPosition(x - 15, y - 15);
                hl.setFillColor(sf::Color(60, 90, 150, 150));
                hl.setOutlineColor(sf::Color(150, 210, 255));
                hl.setOutlineThickness(2.f);
                target.draw(hl);
            }
            if (tex_ok[i]) {
                sf::Sprite s(tex[i]);
                s.setScale(3.f, 3.f);
                s.setPosition(x, y);
                target.draw(s);
            }
            sf::Text label(SPECIES[i], font, 18);
            label.setPosition(x, y + 90);
            label.setFillColor(sel ? sf::Color(150, 210, 255) : sf::Color::White);
            target.draw(label);
        }

        sf::Text hint("[A/D] waehlen   [SPACE] bestaetigen", font, 15);
        hint.setPosition(size.x * 0.5f - 130, size.y - 40);
        hint.setFillColor(sf::Color(180, 180, 180));
        target.draw(hint);
        target.setView(saved);
    }
};

// `msgbox ..., MSGBOX_YESNO` (heal at the Pokemon Center, buy/sell
// confirmations, ...): the VM can't drive a cursor-driven choice itself
// (same reason as StarterSelect above), so this shows a real Ja/Nein
// prompt and the pick is fed back via ScriptVM::resolve_yesno().
struct YesNoPrompt {
    bool open_ = false, done_ = false;
    int cursor = 0;   // 0 = Ja, 1 = Nein
    sf::Font font; bool font_ok = false;

    bool load() { return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf"); }
    void open() { open_ = true; done_ = false; cursor = 0; }
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
        float w = 120.f, h = 74.f;
        float x = size.x - w - 14.f, y = size.y - h - 100.f;
        sf::RectangleShape box(sf::Vector2f(w, h));
        box.setPosition(x, y);
        box.setFillColor(sf::Color(248, 248, 250));
        box.setOutlineColor(sf::Color(40, 40, 56));
        box.setOutlineThickness(3.f);
        target.draw(box);
        const char* opts[2] = {"Ja", "Nein"};
        for (int i = 0; i < 2; ++i) {
            std::string s = std::string(i == cursor ? "> " : "  ") + opts[i];
            sf::Text t(s, font, 20);
            t.setPosition(x + 16, y + 10 + i * 32);
            t.setFillColor(sf::Color(40, 40, 56));
            target.draw(t);
        }
        target.setView(saved);
    }
};

// "ITEM_POKE_BALL" -> "Poke Ball"
static std::string item_display_name(const std::string& item) {
    std::string s = item;
    if (s.rfind("ITEM_", 0) == 0) s = s.substr(5);
    std::string out; bool cap = true;
    for (char c : s) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
        else out += (char)std::tolower((unsigned char)c);
    }
    return out;
}

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

    bool load() { return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf"); }
    void configure(GameState* g, const std::unordered_map<std::string, int>* p) {
        this->gs = g; this->prices = p;
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
        sf::RectangleShape bg(size);
        bg.setFillColor(sf::Color(20, 28, 48, 250));
        target.draw(bg);

        sf::Text money_t("Geld: " + std::to_string(gs ? gs->money : 0) + " P", font, 18);
        money_t.setPosition(20, 16);
        money_t.setFillColor(sf::Color(255, 220, 120));
        target.draw(money_t);

        if (stage == MSG) {
            sf::Text t(sf::String::fromUtf8(flash.begin(), flash.end()), font, 20);
            t.setPosition(size.x * 0.5f - 140, size.y * 0.5f - 10);
            t.setFillColor(sf::Color::White);
            target.draw(t);
            sf::Text hint("[SPACE] weiter", font, 14);
            hint.setPosition(size.x * 0.5f - 60, size.y * 0.5f + 30);
            hint.setFillColor(sf::Color(180, 180, 180));
            target.draw(hint);
            target.setView(saved);
            return;
        }

        float y0 = 60.f;
        for (size_t i = 0; i < items.size(); ++i) {
            bool sel = (int)i == cursor;
            std::string line = item_display_name(items[i]) + "  " +
                                std::to_string(price_of(items[i])) + " P";
            sf::Text t((sel ? "> " : "  ") + line, font, 18);
            t.setPosition(30, y0 + i * 26);
            t.setFillColor(sel ? sf::Color(150, 210, 255) : sf::Color::White);
            target.draw(t);
        }
        {
            bool sel = cursor == (int)items.size();
            std::string s = std::string(sel ? "> " : "  ") + "Verlassen";
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, 18);
            t.setPosition(30, y0 + items.size() * 26 + 10);
            t.setFillColor(sel ? sf::Color(150, 210, 255) : sf::Color::White);
            target.draw(t);
        }

        if (stage == QTY) {
            std::string s = "Anzahl: " + std::to_string(qty) + "  (" +
                             std::to_string(price_of(items[cursor]) * qty) + " P)";
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, 18);
            t.setPosition(size.x * 0.5f - 60, size.y - 60);
            t.setFillColor(sf::Color(150, 210, 255));
            target.draw(t);
            sf::Text hint("[W/S] Anzahl  [A] zurueck  [SPACE] kaufen", font, 14);
            hint.setPosition(size.x * 0.5f - 150, size.y - 30);
            hint.setFillColor(sf::Color(180, 180, 180));
            target.draw(hint);
        } else {
            sf::Text hint("[W/S] waehlen  [SPACE] auswaehlen", font, 14);
            hint.setPosition(30, size.y - 30);
            hint.setFillColor(sf::Color(180, 180, 180));
            target.draw(hint);
        }
        target.setView(saved);
    }
};

int main() {
    const char* map_env = std::getenv("CODEMON_MAP");
    // Story start: the player wakes up in their bedroom on Brendan's House 2F
    // (heal location HEAL_LOCATION_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F, tile 4,2),
    // then walks downstairs and out into Littleroot Town.
    const char* start_map = map_env ? map_env : "maps/LittlerootTown_BrendansHouse_2F.map";
    std::mt19937 rng(1234);

    GameState gs;
    // New-game default world state: every NPC/item hidden until its own
    // story beat unlocks it (data/scripts/new_game.inc in pokeemerald),
    // e.g. the rival isn't standing in your bedroom, Birch isn't already
    // in his lab, Mom's "moving in" dialogue is used instead of the daily one.
    {
        std::ifstream ngf("assets/new_game_flags.txt");
        std::string ln;
        while (std::getline(ngf, ln))
            if (!ln.empty()) gs.set_flag(ln);
    }
    Session* sess = load_session(start_map, -1, -1, &gs);

    const unsigned win_w = VIEW_TW * sess->map->get_tile_size() * SCALE;
    const unsigned win_h = VIEW_TH * sess->map->get_tile_size() * SCALE;

    DialogBox box;
    box.load_font();
    BattleData bdata;
    bdata.load("assets/battle");
    std::vector<Mon> team;                          // the player's party
    team.reserve(6);                                // keep &team[0] stable
    team.push_back(bdata.make_mon("TREECKO", 8));   // starter
    std::vector<Mon> pc_box;                         // PC storage
    Battle battle;
    battle.configure(&bdata, &rng);
    battle.set_capture(&gs, &team, &pc_box);
    ScriptVM vm;
    vm.set_battle_data(&bdata, &team[0]);
    vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player);
    run_load_triggers(sess->map, gs, vm);
    Menu menu;
    menu.load_font();
    menu.configure(&gs, &team, &pc_box, &bdata);
    Minigame games;
    games.load_font();
    games.configure(&gs, &rng);
    StarterSelect starter;
    starter.load();
    if (std::getenv("CODEMON_CHOOSE_STARTER")) starter.open();
    YesNoPrompt yesno;
    yesno.load();
    std::unordered_map<std::string, int> item_prices;
    {
        std::ifstream pf("assets/items/prices.tsv");
        std::string ln;
        while (std::getline(pf, ln)) {
            size_t tab = ln.find('\t');
            if (tab == std::string::npos) continue;
            item_prices[ln.substr(0, tab)] = std::atoi(ln.c_str() + tab + 1);
        }
    }
    Shop shop;
    shop.load();
    shop.configure(&gs, &item_prices);
    // a few starting items so the bag is not empty
    gs.give_item("ITEM_POTION", 5);
    gs.give_item("ITEM_POKE_BALL", 10);
    gs.give_item("ITEM_ANTIDOTE", 2);
    gs.give_item("ITEM_TM19", 1);   // Giga Drain  (teachable in the bag)
    gs.give_item("ITEM_TM31", 1);   // Brick Break
    gs.give_item("ITEM_TM40", 1);   // Aerial Ace
    gs.give_item("ITEM_HM01", 1);   // Cut (reusable HM)
    gs.set_var("COINS", 50);
    // demo hook: grant EXP to the starter to show level-ups / evolution
    if (const char* xe = std::getenv("CODEMON_GRANT_EXP")) {
        std::vector<std::string> xm;
        bdata.grant_exp(team[0], atol(xe), xm);
        std::string joined;
        for (size_t i = 0; i < xm.size(); ++i) { if (i) joined += '\x1f'; joined += xm[i]; }
        if (!joined.empty()) box.open("", joined);
    }
    bool force_enc = std::getenv("CODEMON_FORCE_ENCOUNTER") != nullptr;

    // map-name banner + warp fade-in state
    std::string banner = pretty_map(start_map);
    float banner_t = 2.2f, fade = 1.0f;
    sf::Font ban_font; ban_font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    auto on_map_change = [&](const std::string& path) {
        banner = pretty_map(path); banner_t = 2.2f; fade = 1.0f;
        menu.set_location(banner);
    };
    // A script-driven `warp` (e.g. Route 101 Birch's bag sending the player
    // to his lab after picking a starter) swaps the session the same way
    // stepping onto a warp tile does.
    auto do_pending_warp = [&](Audio* aud) {
        if (!vm.has_pending_warp()) return;
        std::string dest; int wx, wy;
        vm.get_pending_warp(dest, wx, wy);
        vm.clear_pending_warp();
        if (dest == "-") return;
        Session* ns = load_session("maps/" + dest + ".map", wx, wy, &gs);
        if (!ns->map->ready()) { free_session(ns); return; }
        free_session(sess);
        sess = ns;
        vm.configure(sess->map, &gs, &box, &battle, aud, sess->player);
        run_load_triggers(sess->map, gs, vm);
        on_map_change(sess->path);
    };
    menu.set_location(banner);

    // Map a walk token to a battle button (for scripted battle demos).
    auto token_btn = [](char t) -> BtnInput {
        switch (t) { case 'N': return BTN_UP; case 'S': return BTN_DOWN;
                     case 'W': return BTN_LEFT; case 'E': return BTN_RIGHT;
                     default: return BTN_CONFIRM; }
    };

    // --- headless screenshot / animation mode ------------------------------
    if (const char* shot = std::getenv("CODEMON_SCREENSHOT")) {
        int frames = 1;
        if (const char* fe = std::getenv("CODEMON_FRAMES")) frames = std::max(1, atoi(fe));
        std::vector<char> walk = parse_walk(std::getenv("CODEMON_WALK"));
        sf::RenderTexture rt;
        if (rt.create(win_w, win_h)) {
            for (int i = 0; i < frames; ++i) {
                if (i > 0) {
                    char tok = ((size_t)(i - 1) < walk.size()) ? walk[i - 1] : 0;
                    if (starter.active()) {
                        if (tok) starter.input(token_btn(tok));
                    } else if (yesno.active()) {
                        if (tok) yesno.input(token_btn(tok));
                    } else if (shop.active()) {
                        if (tok) shop.input(token_btn(tok));
                    } else if (battle.active()) {
                        if (tok) battle.input(token_btn(tok));
                    } else if (games.active()) {
                        if (tok) games.input(token_btn(tok));
                    } else if (tok == 'G') {
                        games.open();
                    } else if (menu.active()) {
                        if (tok == 'M') menu.close();
                        else if (tok) menu.input(token_btn(tok));
                    } else if (tok == 'M') {
                        menu.open();
                    } else if (vm.running()) {
                        if (tok == 'T') vm.on_key();
                        vm.update(0.13f);
                    } else if (tok == 'T') {
                        interact(sess, box, nullptr, vm);
                    } else if (!box.is_active()) {
                        if (tok) {
                            int pbx = sess->player->get_tile_x();
                            int pby = sess->player->get_tile_y();
                            Session* before = sess;
                            sess = player_step(sess, char_to_dir(tok), nullptr, &gs);
                            if (sess != before) {
                                vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player);
    run_load_triggers(sess->map, gs, vm);
                                on_map_change(sess->path);
                            } else if (sess->player->get_tile_x() != pbx ||
                                       sess->player->get_tile_y() != pby) {
                                try_encounter(sess, battle, team[0], rng, force_enc);
                                check_trigger(sess, vm, gs);
                            }
                        }
                        if (walk.empty()) tick_npcs(sess, rng);
                    }
                    // Route 101's Birch's-bag script blocks on `special
                    // ChooseStarter`; show the real chooser and feed the
                    // pick back so the script (and its Pokedex/heal/warp
                    // follow-up) continues.
                    if (starter.done()) {
                        if (vm.wants_starter()) vm.resolve_starter(starter.chosen());
                        else team[0] = bdata.make_mon(starter.chosen(), 5);
                        starter.ack();
                    } else if (!starter.active() && vm.wants_starter()) {
                        starter.open();
                    }
                    if (yesno.done()) {
                        if (vm.wants_yesno()) vm.resolve_yesno(yesno.yes());
                        yesno.ack();
                    } else if (!yesno.active() && vm.wants_yesno()) {
                        yesno.open();
                    }
                    if (shop.done()) {
                        vm.close_shop();
                        shop.ack();
                    } else if (!shop.active() && vm.wants_shop()) {
                        const std::vector<std::string>* sitems = vm.shop_items();
                        if (sitems && !sitems->empty()) shop.open(*sitems);
                        else vm.close_shop();
                    }
                    do_pending_warp(nullptr);
                    battle.tick(0.13f);
                    games.tick(0.13f);
                    if (banner_t > 0.f) banner_t -= 0.13f;
                    if (fade > 0.f) fade -= 0.13f * 1.6f;
                }
                rt.clear(sf::Color(40, 72, 56));
                if (starter.active()) starter.draw(rt);
                else if (shop.active()) shop.draw(rt);
                else if (battle.active()) battle.draw(rt);
                else if (games.active()) games.draw(rt);
                else {
                    draw_scene(rt, sess); box.draw(rt);
                    draw_banner(rt, ban_font, banner, banner_t);
                    menu.draw(rt);
                    if (yesno.active()) yesno.draw(rt);
                    if (fade > 0.f) {
                        rt.setView(rt.getDefaultView());
                        sf::RectangleShape f(rt.getView().getSize());
                        f.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)(std::min(1.f, fade) * 255)));
                        rt.draw(f);
                    }
                }
                rt.display();
                char name[512];
                if (frames == 1) std::snprintf(name, sizeof(name), "%s", shot);
                else std::snprintf(name, sizeof(name), "%s_%03d.png", shot, i);
                rt.getTexture().copyToImage().saveToFile(name);
            }
        }
        free_session(sess);
        return 0;
    }

    // --- interactive game --------------------------------------------------
    Audio audio; audio.load("assets");
    vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player);
    run_load_triggers(sess->map, gs, vm);
    Window scr(win_w, win_h, "Codemon!");

    sf::Clock clock; float npc_accum = 0.f;
    while (scr.get_window()->isOpen()) {
        sf::Event event;
        while (scr.get_event(&event)) {
            if (event.type == sf::Event::Closed) scr.close();
            else if (event.type == sf::Event::KeyPressed) {
                if (starter.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::A: starter.input(BTN_LEFT); break;
                    case sf::Keyboard::D: starter.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: starter.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (yesno.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: yesno.input(BTN_UP); break;
                    case sf::Keyboard::S: yesno.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: yesno.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (shop.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: shop.input(BTN_UP); break;
                    case sf::Keyboard::S: shop.input(BTN_DOWN); break;
                    case sf::Keyboard::A: shop.input(BTN_LEFT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: shop.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (battle.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: battle.input(BTN_UP); break;
                    case sf::Keyboard::S: battle.input(BTN_DOWN); break;
                    case sf::Keyboard::A: battle.input(BTN_LEFT); break;
                    case sf::Keyboard::D: battle.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: battle.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (games.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: games.input(BTN_UP); break;
                    case sf::Keyboard::S: games.input(BTN_DOWN); break;
                    case sf::Keyboard::A: games.input(BTN_LEFT); break;
                    case sf::Keyboard::D: games.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: games.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (event.key.code == sf::Keyboard::G &&
                           !box.is_active() && !vm.running() && !menu.active()) {
                    games.open();
                } else if (menu.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: menu.input(BTN_UP); break;
                    case sf::Keyboard::S: menu.input(BTN_DOWN); break;
                    case sf::Keyboard::A: menu.input(BTN_LEFT); break;
                    case sf::Keyboard::D: menu.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: menu.input(BTN_CONFIRM); break;
                    case sf::Keyboard::M: menu.close(); break;
                    default: break;
                    }
                } else if (event.key.code == sf::Keyboard::M &&
                           !box.is_active() && !vm.running()) {
                    menu.open();
                } else if (event.key.code == sf::Keyboard::Space ||
                           event.key.code == sf::Keyboard::Return) {
                    if (vm.running()) vm.on_key();           // advance a script message
                    else interact(sess, box, &audio, vm);    // talk / advance / dismiss
                } else if (!box.is_active() && !vm.running()) {
                    DIR dir = convert_key_event(event);
                    if (dir != DIR::NONE) {
                        int pbx = sess->player->get_tile_x();
                        int pby = sess->player->get_tile_y();
                        Session* before = sess;
                        sess = player_step(sess, dir, &audio, &gs);
                        if (sess != before) {
                            vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player);
    run_load_triggers(sess->map, gs, vm);
                            on_map_change(sess->path);
                        } else if (sess->player->get_tile_x() != pbx ||
                                   sess->player->get_tile_y() != pby) {
                            try_encounter(sess, battle, team[0], rng, false);
                            check_trigger(sess, vm, gs);
                        }
                    }
                }
            }
        }
        if (starter.done()) {
            if (vm.wants_starter()) vm.resolve_starter(starter.chosen());
            else team[0] = bdata.make_mon(starter.chosen(), 5);
            starter.ack();
        } else if (!starter.active() && vm.wants_starter()) {
            starter.open();
        }
        if (yesno.done()) {
            if (vm.wants_yesno()) vm.resolve_yesno(yesno.yes());
            yesno.ack();
        } else if (!yesno.active() && vm.wants_yesno()) {
            yesno.open();
        }
        if (shop.done()) {
            vm.close_shop();
            shop.ack();
        } else if (!shop.active() && vm.wants_shop()) {
            const std::vector<std::string>* sitems = vm.shop_items();
            if (sitems && !sitems->empty()) shop.open(*sitems);
            else vm.close_shop();
        }
        do_pending_warp(&audio);
        float dt = clock.restart().asSeconds();
        if (vm.running()) vm.update(dt);
        battle.tick(dt);
        games.tick(dt);
        if (banner_t > 0.f) banner_t -= dt;
        if (fade > 0.f) fade -= dt * 1.6f;
        // NPCs freeze while a dialog/battle/script/menu/minigame is running.
        npc_accum += dt;
        if (!box.is_active() && !battle.active() && !vm.running() &&
            !menu.active() && !games.active()) {
            while (npc_accum >= NPC_TICK) { tick_npcs(sess, rng); npc_accum -= NPC_TICK; }
        } else {
            npc_accum = 0.f;
        }

        scr.clear();
        if (starter.active()) { starter.draw(*scr.get_window()); }
        else if (shop.active()) { shop.draw(*scr.get_window()); }
        else if (battle.active()) { battle.draw(*scr.get_window()); }
        else if (games.active()) { games.draw(*scr.get_window()); }
        else {
            draw_scene(*scr.get_window(), sess);
            box.draw(*scr.get_window());
            draw_banner(*scr.get_window(), ban_font, banner, banner_t);
            menu.draw(*scr.get_window());
            if (yesno.active()) yesno.draw(*scr.get_window());
            if (fade > 0.f) {
                sf::View sv = scr.get_window()->getView();
                scr.get_window()->setView(scr.get_window()->getDefaultView());
                sf::RectangleShape f(scr.get_window()->getView().getSize());
                f.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)(std::min(1.f, fade) * 255)));
                scr.get_window()->draw(f);
                scr.get_window()->setView(sv);
            }
        }
        scr.display();
        sf::sleep(sf::milliseconds(16));
    }

    free_session(sess);
    return 0;
}
