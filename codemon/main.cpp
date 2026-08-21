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
#include "UiFrame.h"

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

// The Pokemon Center nurse's healing animation (pokeemerald's
// FLDEFF_POKECENTER_HEAL, src/field_effect.c CreateGlowingPokeballsEffect +
// CreatePokecenterMonitorSprite): one small Poke Ball per party member
// glows in front of the counter while the healing machine's screen blinks.
// ScriptVM blocks on `dofieldeffect FLDEFF_POKECENTER_HEAL` and times itself
// out (ScriptVM::HEALFX_DURATION); this just needs to look busy for exactly
// that long, drawn in world space so it stays put next to the player like
// any other actor.
struct HealFx {
    bool active_ = false;
    float t = 0.f;
    int n_balls = 1;
    sf::Texture ball_tex, mon0_tex, mon1_tex;
    bool ok = false;

    void load() {
        ok = ball_tex.loadFromFile("assets/graphics/field_effects/pics/pokeball_glow.png");
        ok = mon0_tex.loadFromFile("assets/graphics/field_effects/pics/pokecenter_monitor/0.png") && ok;
        ok = mon1_tex.loadFromFile("assets/graphics/field_effects/pics/pokecenter_monitor/1.png") && ok;
        ball_tex.setSmooth(false); mon0_tex.setSmooth(false); mon1_tex.setSmooth(false);
    }
    void start(int party_size) {
        active_ = true; t = 0.f;
        n_balls = std::max(1, std::min(6, party_size));
    }
    bool active() const { return active_; }
    void tick(float dt) {
        if (!active_) return;
        t += dt;
        if (t >= ScriptVM::HEALFX_DURATION) active_ = false;
    }

    // Centered above the world pixel position (wx,wy) -- call while the
    // target's view is still the world camera (i.e. from inside draw_scene).
    void draw(sf::RenderTarget& target, float wx, float wy) const {
        if (!active_ || !ok) return;
        const float scale = 2.5f;
        bool bright = std::fmod(t, 0.3f) < 0.15f;
        float bw = 8.f * scale;
        float total = n_balls * bw;
        for (int i = 0; i < n_balls; ++i) {
            sf::Sprite s(ball_tex);
            s.setScale(scale, scale);
            s.setColor(bright ? sf::Color::White : sf::Color(255, 255, 255, 110));
            s.setPosition(wx - total / 2.f + i * bw, wy - 30.f);
            target.draw(s);
        }
        sf::Sprite ms(bright ? mon1_tex : mon0_tex);
        ms.setScale(scale, scale);
        ms.setPosition(wx - 30.f, wy - 54.f);
        target.draw(ms);
    }
};

static void draw_scene(sf::RenderTarget& target, Session* s, const HealFx* healfx = nullptr) {
    target.setView(camera_for(s));
    s->map->render_to(target);
    std::sort(s->actors.begin(), s->actors.end(),
              [](Character* a, Character* b) { return a->get_tile_y() < b->get_tile_y(); });
    for (Character* a : s->actors) {
        a->update_sprite(s->map->get_tile_size());
        target.draw(*a->get_current_sprite());
    }
    if (healfx && healfx->active() && s->player) {
        int tp = s->map->get_tile_size();
        float wx = s->player->get_tile_x() * tp + tp * 0.5f;
        float wy = s->player->get_tile_y() * tp + tp * 0.5f;
        healfx->draw(target, wx, wy);
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

    bool load() {
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    }
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
        float w = 132.f, h = 78.f;
        float x = size.x - w - 14.f, y = size.y - h - 100.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
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
    vm.set_battle_data(&bdata, &team);
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
    HealFx healfx;
    healfx.load();
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
                    if (!healfx.active() && vm.wants_heal_fx()) healfx.start((int)team.size());
                    healfx.tick(0.13f);
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
                    draw_scene(rt, sess, &healfx); box.draw(rt);
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
        if (!healfx.active() && vm.wants_heal_fx()) healfx.start((int)team.size());
        do_pending_warp(&audio);
        float dt = clock.restart().asSeconds();
        healfx.tick(dt);
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
            draw_scene(*scr.get_window(), sess, &healfx);
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
