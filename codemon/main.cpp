#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Coordinates.h"
#include "MainMenu.h"
#include "view_utils.h"

#include <string>

// Fixed virtual/design resolution the whole game is drawn against. The result
// is scaled to the actual (resizable / fullscreen) window through an SFML view.
static const unsigned int VIRTUAL_W = 32 * 16;
static const unsigned int VIRTUAL_H = 32 * 16;
static const float TILE = 32.f;

enum class AppState { MainMenu, Playing };

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// A view that scales the virtual resolution into the window while keeping the
// aspect ratio, centred on (cam_x, cam_y) but clamped so the camera never
// shows past the edges of a map that is at least as big as the view.
static sf::View make_view(unsigned int win_w, unsigned int win_h,
                          float cam_x, float cam_y,
                          float map_px_w, float map_px_h) {
    sf::View view(sf::FloatRect(0.f, 0.f, (float)VIRTUAL_W, (float)VIRTUAL_H));

    const float half_w = VIRTUAL_W / 2.f;
    const float half_h = VIRTUAL_H / 2.f;

    float cx = (map_px_w <= VIRTUAL_W) ? map_px_w / 2.f : clampf(cam_x, half_w, map_px_w - half_w);
    float cy = (map_px_h <= VIRTUAL_H) ? map_px_h / 2.f : clampf(cam_y, half_h, map_px_h - half_h);
    view.setCenter(cx, cy);

    ViewportRect vp = letterbox_viewport(win_w, win_h, (float)VIRTUAL_W, (float)VIRTUAL_H);
    view.setViewport(sf::FloatRect(vp.x, vp.y, vp.w, vp.h));
    return view;
}

// Movement direction as a tile delta, matching Character::move().
static void dir_delta(DIR dir, int& dx, int& dy) {
    dx = 0; dy = 0;
    switch (dir) {
    case DIR::N: dy = -1; break;
    case DIR::S: dy =  1; break;
    case DIR::E: dx =  1; break;
    case DIR::W: dx = -1; break;
    default: break;
    }
}

static DIR convert_key(sf::Keyboard::Key key) {
    switch (key) {
    case sf::Keyboard::W: return DIR::N;
    case sf::Keyboard::S: return DIR::S;
    case sf::Keyboard::A: return DIR::W;
    case sf::Keyboard::D: return DIR::E;
    default:              return DIR::NONE;
    }
}

//Welcome to Codemon!
int main()
{
    Window scr(VIRTUAL_W, VIRTUAL_H, "Codemon!");

    MainMenu menu((float)VIRTUAL_W, (float)VIRTUAL_H);
    menu.load("assets/fonts/PressStart2P-Regular.ttf");

    Character player;
    player.load_sprite_sheet();
    Map game_map("maps/map_00.txt", "maps/map_set/map_00.png");

    //Spawn the player at the map's start tile.
    Coordinates start = game_map.get_start_pos();
    player.set_x((int)(start.get_x() * TILE));
    player.set_y((int)(start.get_y() * TILE));
    player.update_sprite_pos();

    AppState state = AppState::MainMenu;
    sf::Clock clock;

    while (scr.get_window()->isOpen())
    {
        const float dt = clock.restart().asSeconds();

        sf::Event event;
        while (scr.get_event(&event))
        {
            switch (event.type) {
            case sf::Event::Closed:
                scr.close();
                break;

            case sf::Event::KeyPressed: {
                const sf::Keyboard::Key key = event.key.code;

                //F11 toggles fullscreen in any state.
                if (key == sf::Keyboard::F11) {
                    scr.recreate(!scr.is_fullscreen());
                    break;
                }

                if (state == AppState::MainMenu) {
                    switch (menu.handle_key(key)) {
                    case MenuResult::StartGame: state = AppState::Playing; break;
                    case MenuResult::Quit:      scr.close();               break;
                    default: break;
                    }
                } else { // Playing
                    if (key == sf::Keyboard::Escape) {
                        state = AppState::MainMenu; // back to the menu
                    } else {
                        const DIR dir = convert_key(key);
                        if (dir != DIR::NONE) {
                            //Collision: only move if the target tile is on the map.
                            int dx, dy;
                            dir_delta(dir, dx, dy);
                            const int tile_x = (int)(player.get_x() / (int)TILE) + dx;
                            const int tile_y = (int)(player.get_y() / (int)TILE) + dy;

                            player.set_facing(dir);
                            if (tile_x >= 0 && tile_y >= 0 &&
                                game_map.in_bounds(Coordinates((unsigned int)tile_x, (unsigned int)tile_y))) {
                                player.move(dir);
                            }
                            player.update_sprite_pos();
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
        }

        if (!scr.get_window()->isOpen()) {
            break;
        }

        /* Update */
        if (state == AppState::MainMenu) {
            menu.update(dt);
        }

        /* Determine the view for this frame */
        const sf::Vector2u ws = scr.get_size();
        sf::View view;
        if (state == AppState::Playing) {
            view = make_view(ws.x, ws.y,
                             player.get_x() + TILE / 2.f, player.get_y() + TILE / 2.f,
                             (float)game_map.get_pixel_width(), (float)game_map.get_pixel_height());
        } else {
            view = make_view(ws.x, ws.y, VIRTUAL_W / 2.f, VIRTUAL_H / 2.f, (float)VIRTUAL_W, (float)VIRTUAL_H);
        }
        scr.set_view(view);

        /* Render */
        scr.clear();
        if (state == AppState::MainMenu) {
            menu.render(*scr.get_window());
        } else {
            game_map.render_map(&scr);
            scr.draw(&player);
        }
        scr.display();
    }

    return 0;
}
