#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Audio.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

// Integer zoom factor: the world is authored at native 16px tiles and scaled up.
static const int SCALE = 3;
// Seconds between NPC "think" ticks.
static const float NPC_TICK = 0.45f;

static DIR convert_key_event(const sf::Event& event) {
    switch (event.key.code) {
    case sf::Keyboard::W: return DIR::N;
    case sf::Keyboard::S: return DIR::S;
    case sf::Keyboard::A: return DIR::W;
    case sf::Keyboard::D: return DIR::E;
    default:              return DIR::NONE;
    }
}

// An NPC plus its behaviour state.
struct Agent {
    Character* ch;
    MoveKind kind;
    DIR pace_dir;
    int home_x, home_y;
};

static bool actor_at(const std::vector<Character*>& actors, Character* self,
                     int tx, int ty) {
    for (Character* a : actors) {
        if (a == self) continue;
        if (a->get_tile_x() == tx && a->get_tile_y() == ty) return true;
    }
    return false;
}

// Try to move `ag` one tile in `dir`; returns true if it stepped.
static bool try_step(Agent& ag, DIR dir, Map& map,
                     std::vector<Character*>& actors) {
    int tx, ty;
    ag.ch->set_facing(dir);
    ag.ch->target_tile(dir, tx, ty);
    if (map.passable(tx, ty) && !actor_at(actors, ag.ch, tx, ty)) {
        ag.ch->step(dir);
        return true;
    }
    ag.ch->face(dir);
    return false;
}

// Advance every NPC one behaviour tick.
static void tick_npcs(std::vector<Agent>& agents, Map& map,
                      std::vector<Character*>& actors, std::mt19937& rng) {
    static const DIR dirs[4] = {DIR::N, DIR::S, DIR::E, DIR::W};
    for (Agent& ag : agents) {
        switch (ag.kind) {
        case MOVE_WANDER: {
            DIR d = dirs[rng() % 4];
            int tx, ty; ag.ch->target_tile(d, tx, ty);
            // keep a 2-tile leash around the spawn so NPCs stay put-ish
            if (std::abs(tx - ag.home_x) <= 2 && std::abs(ty - ag.home_y) <= 2) {
                try_step(ag, d, map, actors);
            } else {
                ag.ch->face(d);
            }
            break;
        }
        case MOVE_PACE_V:
            if (!try_step(ag, ag.pace_dir, map, actors)) {
                ag.pace_dir = (ag.pace_dir == DIR::N) ? DIR::S : DIR::N;
                try_step(ag, ag.pace_dir, map, actors);
            }
            break;
        case MOVE_PACE_H:
            if (!try_step(ag, ag.pace_dir, map, actors)) {
                ag.pace_dir = (ag.pace_dir == DIR::E) ? DIR::W : DIR::E;
                try_step(ag, ag.pace_dir, map, actors);
            }
            break;
        case MOVE_STATIC:
        default:
            break;
        }
    }
}

static void draw_scene(sf::RenderTarget& target, Map& map,
                       std::vector<Character*>& actors) {
    map.render_to(target);
    std::sort(actors.begin(), actors.end(),
              [](Character* a, Character* b) { return a->get_tile_y() < b->get_tile_y(); });
    for (Character* a : actors) {
        a->update_sprite(map.get_tile_size());
        target.draw(*a->get_current_sprite());
    }
}

int main() {
    const char* map_env = std::getenv("CODEMON_MAP");
    Map game_map(map_env ? map_env : "maps/LittlerootTown.map");
    const int tile_px = game_map.get_tile_size();
    const unsigned world_w = game_map.get_width()  * tile_px;
    const unsigned world_h = game_map.get_height() * tile_px;

    // Player
    Character player(game_map.get_start_pos().get_x(),
                     game_map.get_start_pos().get_y());
    player.load_sprite_sheet("assets/overworld/people_brendan_walking.png");
    player.face(DIR::S);

    // NPCs straight from the map's authored object events.
    std::vector<Agent> agents;
    std::vector<Character*> npc_chars;
    for (const NpcSpawn& s : game_map.npcs()) {
        Character* ch = new Character(s.x, s.y);
        if (!ch->load_sprite_sheet("assets/overworld/" + s.sheet + ".png")) {
            delete ch;
            continue;
        }
        ch->face(s.facing);
        Agent ag;
        ag.ch = ch; ag.kind = s.movement; ag.home_x = s.x; ag.home_y = s.y;
        ag.pace_dir = (s.movement == MOVE_PACE_H) ? DIR::E : DIR::N;
        agents.push_back(ag);
        npc_chars.push_back(ch);
    }

    std::vector<Character*> actors;
    for (Character* n : npc_chars) actors.push_back(n);
    actors.push_back(&player);

    std::mt19937 rng(1234);   // deterministic wandering

    // --- headless screenshot / animation mode ------------------------------
    // CODEMON_SCREENSHOT=path renders one frame. If CODEMON_FRAMES=N is also
    // set, N frames are rendered (advancing NPC movement each frame) and saved
    // as <path>_000.png, <path>_001.png, ... so movement can be shown.
    if (const char* shot = std::getenv("CODEMON_SCREENSHOT")) {
        int frames = 1;
        if (const char* fe = std::getenv("CODEMON_FRAMES")) frames = std::max(1, atoi(fe));
        sf::RenderTexture rt;
        if (rt.create(world_w * SCALE, world_h * SCALE)) {
            rt.setView(sf::View(sf::FloatRect(0.f, 0.f, (float)world_w, (float)world_h)));
            for (int i = 0; i < frames; ++i) {
                if (i > 0) tick_npcs(agents, game_map, actors, rng);
                rt.clear(sf::Color(40, 72, 56));
                draw_scene(rt, game_map, actors);
                rt.display();
                char name[512];
                if (frames == 1) std::snprintf(name, sizeof(name), "%s", shot);
                else std::snprintf(name, sizeof(name), "%s_%03d.png", shot, i);
                rt.getTexture().copyToImage().saveToFile(name);
            }
        }
        for (Character* n : npc_chars) delete n;
        return 0;
    }

    // --- interactive game --------------------------------------------------
    Audio audio;
    audio.load("assets");

    Window scr(world_w * SCALE, world_h * SCALE, "Codemon!");
    scr.get_window()->setView(
        sf::View(sf::FloatRect(0.f, 0.f, (float)world_w, (float)world_h)));

    sf::Clock clock;
    float npc_accum = 0.f;

    while (scr.get_window()->isOpen()) {
        sf::Event event;
        while (scr.get_event(&event)) {
            if (event.type == sf::Event::Closed) {
                scr.close();
            } else if (event.type == sf::Event::KeyPressed) {
                DIR dir = convert_key_event(event);
                if (dir != DIR::NONE) {
                    int tx, ty;
                    player.target_tile(dir, tx, ty);
                    if (game_map.passable(tx, ty) && !actor_at(actors, &player, tx, ty)) {
                        player.step(dir);
                        audio.play_step();
                    } else {
                        player.face(dir);
                        audio.play_bump();
                    }
                }
            }
        }

        // NPCs think on their own clock, independent of player input.
        npc_accum += clock.restart().asSeconds();
        while (npc_accum >= NPC_TICK) {
            tick_npcs(agents, game_map, actors, rng);
            npc_accum -= NPC_TICK;
        }

        scr.clear();
        draw_scene(*scr.get_window(), game_map, actors);
        scr.display();
        sf::sleep(sf::milliseconds(16));
    }

    for (Character* n : npc_chars) delete n;
    return 0;
}
