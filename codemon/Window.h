#pragma once


#include "SFML/Graphics.hpp"
#include "character.h"

//Class for system selements, should be inherited by every like menus, windows, non game stuff
//Will make useful later
class Sys_element
{

};


class Window : private Sys_element
{
private:
    int px_height, px_width;                            //windowed size of the window in pixels
    sf::RenderWindow* win_obj;                          //the underlying render window
    std::string display_text;                           //Text displayed on the screen bar.
    bool fullscreen;                                    //is the window currently fullscreen

public:
    Window(int width, int height, std::string text);

    //Returns openess of windows
    bool is_open();

    bool change_width(unsigned int width_delta);

    bool change_height(unsigned int height_delta);

    sf::RenderWindow* get_window();
    void close();

    void clear();

    void draw(Character* ch_1);
    void draw(sf::Sprite* sprite);
    //Draw any SFML drawable (menu text, shapes, etc.)
    void draw(const sf::Drawable& drawable);


    bool get_event(sf::Event* event);

    void display();

    /* Viewport / resolution handling */
    //Current size of the window in pixels.
    sf::Vector2u get_size();
    //Apply a view (used to scale a fixed virtual resolution to the window).
    void set_view(const sf::View& view);

    /* Fullscreen handling */
    //Recreate the underlying window in fullscreen or windowed mode.
    void recreate(bool fullscreen);
    bool is_fullscreen();

};