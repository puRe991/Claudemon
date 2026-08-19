#include "Window.h"
#include <string>
#include "SFML/Graphics.hpp"

Window::Window(int width, int height, std::string text) {
    this->px_height = height;
    this->px_width = width;
    this->display_text = text;
    this->fullscreen = false;

    //Create Window for the game to take place in; then store  it as a pointer.
    this->win_obj = new sf::RenderWindow(sf::VideoMode(width, height), this->display_text);
    this->win_obj->setFramerateLimit(60);
}

//Returns openess of windows

bool Window::is_open() {
    return this->win_obj->isOpen();
}

bool Window::change_width(unsigned int width_delta) {
    this->px_width += width_delta;
    return true;
}

bool Window::change_height(unsigned int height_delta) {
    this->px_height += height_delta;
    return true;
}

sf::RenderWindow* Window::get_window() {
    return this->win_obj;
}

void Window::close() {
    this->win_obj->close();
}

void Window::clear() {
    this->win_obj->clear(sf::Color::Black);
}

 void Window::draw(Character* ch_1) {
    sf::Sprite* sprite = ch_1->get_current_sprite();
    this->win_obj->draw(*sprite);
}

 void Window::draw(sf::Sprite *sprite) {
     this->win_obj->draw(*sprite);
 }

 void Window::draw(const sf::Drawable& drawable) {
     this->win_obj->draw(drawable);
 }

 bool Window::get_event(sf::Event* event) {
    return this->win_obj->pollEvent(*event);
}

 void Window::display() {
    this->win_obj->display();
}

sf::Vector2u Window::get_size() {
    return this->win_obj->getSize();
}

void Window::set_view(const sf::View& view) {
    this->win_obj->setView(view);
}

void Window::recreate(bool fullscreen) {
    if (this->win_obj) {
        this->win_obj->close();
        delete this->win_obj;
        this->win_obj = nullptr;
    }

    if (fullscreen) {
        this->win_obj = new sf::RenderWindow(
            sf::VideoMode::getDesktopMode(), this->display_text, sf::Style::Fullscreen);
    } else {
        this->win_obj = new sf::RenderWindow(
            sf::VideoMode(this->px_width, this->px_height), this->display_text, sf::Style::Default);
    }

    this->fullscreen = fullscreen;
    this->win_obj->setFramerateLimit(60);
}

bool Window::is_fullscreen() {
    return this->fullscreen;
}