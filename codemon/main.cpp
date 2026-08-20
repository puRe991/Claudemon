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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sstream>
#include <string>
#include <vector>

static const int SCALE = 3;
static const float NPC_TICK = 0.45f;
// Fixed camera viewport in tiles (so the window size is independent of the map).
static const int VIEW_TW = 16, VIEW_TH = 12;
static const char* PLAYER_SHEET = "assets/overworld/people_brendan_walking.png";

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
static Session* load_session(const std::string& path, int arr_x, int arr_y) {
    Session* s = new Session();
    s->map = new Map(path);
    int px = (arr_x >= 0) ? arr_x : (int)s->map->get_start_pos().get_x();
    int py = (arr_y >= 0) ? arr_y : (int)s->map->get_start_pos().get_y();

    s->player = new Character(px, py);
    s->player->load_sprite_sheet(PLAYER_SHEET);
    s->player->face(DIR::S);

    for (const NpcSpawn& sp : s->map->npcs()) {
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
static Session* player_step(Session* s, DIR dir, Audio* audio) {
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
        Session* ns = load_session("maps/" + wp->dest + ".map", -1, -1);
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
static bool interact(Session* s, DialogBox& box, Audio* audio, ScriptVM& vm) {
    if (box.is_active()) { box.advance(); return true; }   // next page / dismiss
    int tx, ty;
    s->player->target_tile(s->player->get_facing(), tx, ty);
    // 1) an NPC on the faced tile
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
    // 2) a readable sign on the faced tile
    const Sign* sg = s->map->sign_at(tx, ty);
    if (sg) {
        box.open("", sg->text);
        if (audio) audio->play_select();
        return true;
    }
    return false;
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
    if (!s->map->is_grass(px, py) || !s->map->has_encounters()) return;
    if (!force && (rng() % 100) >= 22) return;                 // ~22% per grass step
    const std::vector<std::string>& pool = s->map->encounters();
    std::string sp = pool[rng() % pool.size()];
    int level = 3 + (int)(rng() % 4);                          // wild level 3..6
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
        if (t.size() == 1 && std::string("NSEWT").find(t[0]) != std::string::npos)
            out.push_back(t[0]);
    }
    return out;
}

static DIR char_to_dir(char c) {
    switch (c) { case 'N': return DIR::N; case 'S': return DIR::S;
                 case 'E': return DIR::E; case 'W': return DIR::W;
                 default: return DIR::NONE; }
}

int main() {
    const char* map_env = std::getenv("CODEMON_MAP");
    Session* sess = load_session(map_env ? map_env : "maps/LittlerootTown.map", -1, -1);
    std::mt19937 rng(1234);

    const unsigned win_w = VIEW_TW * sess->map->get_tile_size() * SCALE;
    const unsigned win_h = VIEW_TH * sess->map->get_tile_size() * SCALE;

    DialogBox box;
    box.load_font();
    GameState gs;
    BattleData bdata;
    bdata.load("assets/battle");
    Mon party = bdata.make_mon("TREECKO", 8);     // the player's starter
    Battle battle;
    battle.configure(&bdata, &rng);
    ScriptVM vm;
    vm.set_battle_data(&bdata, &party);
    vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player);
    bool force_enc = std::getenv("CODEMON_FORCE_ENCOUNTER") != nullptr;

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
                    if (battle.active()) {
                        if (tok) battle.input(token_btn(tok));
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
                            sess = player_step(sess, char_to_dir(tok), nullptr);
                            if (sess != before)
                                vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player);
                            else if (sess->player->get_tile_x() != pbx ||
                                     sess->player->get_tile_y() != pby) {
                                try_encounter(sess, battle, party, rng, force_enc);
                                check_trigger(sess, vm, gs);
                            }
                        }
                        if (walk.empty()) tick_npcs(sess, rng);
                    }
                }
                rt.clear(sf::Color(40, 72, 56));
                if (battle.active()) battle.draw(rt);
                else { draw_scene(rt, sess); box.draw(rt); }
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
    Window scr(win_w, win_h, "Codemon!");

    sf::Clock clock; float npc_accum = 0.f;
    while (scr.get_window()->isOpen()) {
        sf::Event event;
        while (scr.get_event(&event)) {
            if (event.type == sf::Event::Closed) scr.close();
            else if (event.type == sf::Event::KeyPressed) {
                if (battle.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: battle.input(BTN_UP); break;
                    case sf::Keyboard::S: battle.input(BTN_DOWN); break;
                    case sf::Keyboard::A: battle.input(BTN_LEFT); break;
                    case sf::Keyboard::D: battle.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: battle.input(BTN_CONFIRM); break;
                    default: break;
                    }
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
                        sess = player_step(sess, dir, &audio);
                        if (sess != before) {
                            vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player);
                        } else if (sess->player->get_tile_x() != pbx ||
                                   sess->player->get_tile_y() != pby) {
                            try_encounter(sess, battle, party, rng, false);
                            check_trigger(sess, vm, gs);
                        }
                    }
                }
            }
        }
        float dt = clock.restart().asSeconds();
        if (vm.running()) vm.update(dt);
        // NPCs freeze while a dialog/battle/script is running.
        npc_accum += dt;
        if (!box.is_active() && !battle.active() && !vm.running()) {
            while (npc_accum >= NPC_TICK) { tick_npcs(sess, rng); npc_accum -= NPC_TICK; }
        } else {
            npc_accum = 0.f;
        }

        scr.clear();
        if (battle.active()) { battle.draw(*scr.get_window()); }
        else { draw_scene(*scr.get_window(), sess); box.draw(*scr.get_window()); }
        scr.display();
        sf::sleep(sf::milliseconds(16));
    }

    free_session(sess);
    return 0;
}
