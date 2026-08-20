#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Audio.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// Integer zoom factor: the world is authored at native 16px tiles and scaled
// up so the classic GBA look stays crisp.
static const int SCALE = 3;

// Convert a WASD key event into a movement direction.
static DIR convert_key_event(const sf::Event& event) {
    switch (event.key.code) {
    case sf::Keyboard::W: return DIR::N;
    case sf::Keyboard::S: return DIR::S;
    case sf::Keyboard::A: return DIR::W;
    case sf::Keyboard::D: return DIR::E;
    default:              return DIR::NONE;
    }
}

// Is another actor already standing on (tx, ty)?
static bool actor_at(const std::vector<Character*>& actors, Character* self,
                     int tx, int ty) {
    for (Character* a : actors) {
        if (a == self) continue;
        if (a->get_tile_x() == tx && a->get_tile_y() == ty) return true;
    }
    return false;
}

// Draw the whole scene (map + actors, actors sorted so lower ones overlap
// higher ones) into any SFML target.
static void draw_scene(sf::RenderTarget& target, Map& map,
                       std::vector<Character*>& actors) {
    map.render_to(target);

    std::sort(actors.begin(), actors.end(),
              [](Character* a, Character* b) {
                  return a->get_tile_y() < b->get_tile_y();
              });
    for (Character* a : actors) {
        a->update_sprite(map.get_tile_size());
        target.draw(*a->get_current_sprite());
    }
}

int main() {
    // --- world / assets ----------------------------------------------------
    Map game_map("maps/route.map");
    const int tile_px = game_map.get_tile_size();
    const unsigned world_w = game_map.get_width()  * tile_px;
    const unsigned world_h = game_map.get_height() * tile_px;

    // Player
    Character player(game_map.get_start_pos().get_x(),
                     game_map.get_start_pos().get_y());
    player.load_sprite_sheet("assets/overworld/people_brendan_walking.png");
    player.face(DIR::S);

    // A handful of NPCs placed around the route, each from an imported sheet.
    std::vector<Character*> npcs;
    struct NpcDef { const char* sheet; int x, y; DIR facing; };
    const NpcDef defs[] = {
        {"assets/overworld/people_lass.png",       10,  9, DIR::W},
        {"assets/overworld/people_fisherman.png",  15,  9, DIR::S},
        {"assets/overworld/people_boy_1.png",       5,  9, DIR::E},
        {"assets/overworld/people_beauty.png",      8,  2, DIR::S},
        {"assets/overworld/people_black_belt.png",  2, 12, DIR::N},
    };
    for (const NpcDef& d : defs) {
        Character* npc = new Character(d.x, d.y);
        if (npc->load_sprite_sheet(d.sheet)) {
            npc->face(d.facing);
            npcs.push_back(npc);
        } else {
            delete npc;
        }
    }

    std::vector<Character*> actors;
    for (Character* n : npcs) actors.push_back(n);
    actors.push_back(&player);

    // Audio (silent-safe if the SFX are missing).
    Audio audio;
    audio.load("assets");

    // --- headless screenshot mode -----------------------------------------
    // If CODEMON_SCREENSHOT is set, render one frame off-screen and exit. This
    // works under a virtual display (xvfb) without opening a real window.
    if (const char* shot = std::getenv("CODEMON_SCREENSHOT")) {
        sf::RenderTexture rt;
        if (rt.create(world_w * SCALE, world_h * SCALE)) {
            rt.setView(sf::View(sf::FloatRect(0.f, 0.f,
                                              (float)world_w, (float)world_h)));
            rt.clear(sf::Color(40, 72, 56));
            draw_scene(rt, game_map, actors);
            rt.display();
            rt.getTexture().copyToImage().saveToFile(shot);
        }
        for (Character* n : npcs) delete n;
        return 0;
    }

    // --- interactive game --------------------------------------------------
    Window scr(world_w * SCALE, world_h * SCALE, "Codemon!");
    scr.get_window()->setView(
        sf::View(sf::FloatRect(0.f, 0.f, (float)world_w, (float)world_h)));

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
                    if (game_map.passable(tx, ty) &&
                        !actor_at(actors, &player, tx, ty)) {
                        player.step(dir);
                        audio.play_step();
                    } else {
                        player.face(dir);   // turn to face the obstacle
                        audio.play_bump();
                    }
                }
            }
        }

        scr.clear();
        draw_scene(*scr.get_window(), game_map, actors);
        scr.display();
    }

    for (Character* n : npcs) delete n;
    return 0;
}
