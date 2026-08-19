#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "MainMenu.h"
#include "view_utils.h"

#include <string>

// Fixed virtual/design resolution the whole game is drawn against. The result
// is scaled to the actual (resizable / fullscreen) window through an SFML view.
static const unsigned int VIRTUAL_W = 32 * 16;
static const unsigned int VIRTUAL_H = 32 * 16;

enum class AppState { MainMenu, Playing };

// Build a view that maps the virtual resolution into the window while keeping
// the aspect ratio (letterbox/pillarbox bars instead of stretching).
static sf::View make_scaled_view(unsigned int win_w, unsigned int win_h) {
    sf::View view(sf::FloatRect(0.f, 0.f, (float)VIRTUAL_W, (float)VIRTUAL_H));
    ViewportRect vp = letterbox_viewport(win_w, win_h, (float)VIRTUAL_W, (float)VIRTUAL_H);
    view.setViewport(sf::FloatRect(vp.x, vp.y, vp.w, vp.h));
    return view;
}

// Convert a movement key into a direction for the game state.
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
    //Main screen. Starts at the virtual resolution; freely resizable afterwards.
    Window scr(VIRTUAL_W, VIRTUAL_H, "Codemon!");

    //Title screen / main menu.
    MainMenu menu((float)VIRTUAL_W, (float)VIRTUAL_H);
    menu.load("assets/fonts/PressStart2P-Regular.ttf");

    //Game world objects (used once the player starts a game).
    Character player;
    player.load_sprite_sheet();
    Map game_map("maps/map_00.txt", "maps/map_set/map_00.png");

    AppState state = AppState::MainMenu;

    //Scaled view, kept in sync with the window size.
    sf::View view = make_scaled_view(scr.get_size().x, scr.get_size().y);
    scr.set_view(view);

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

            //Window resized: recompute the scaled view so the picture follows
            //the new window size without distortion.
            case sf::Event::Resized:
                view = make_scaled_view(event.size.width, event.size.height);
                scr.set_view(view);
                break;

            case sf::Event::KeyPressed: {
                const sf::Keyboard::Key key = event.key.code;

                //F11 toggles fullscreen in any state.
                if (key == sf::Keyboard::F11) {
                    scr.recreate(!scr.is_fullscreen());
                    view = make_scaled_view(scr.get_size().x, scr.get_size().y);
                    scr.set_view(view);
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
                            //Bounds check (collision handling still a work in progress).
                            game_map.in_bounds(player.move_cord(dir));
                            player.move(dir);
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

        //A key handler above may have closed the window.
        if (!scr.get_window()->isOpen()) {
            break;
        }

        /* Update */
        if (state == AppState::MainMenu) {
            menu.update(dt);
        }

        /* Render */
        scr.clear();
        scr.set_view(view);
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
