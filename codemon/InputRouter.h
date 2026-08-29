#pragma once
#include "SFML/Graphics.hpp"
#include "Battle.h"   // BtnInput

// Single source of truth for the WASD/Space-or-Enter/Backspace -> BtnInput
// mapping shared by every menu-shaped UI screen (StarterSelect, YesNoPrompt,
// PartyPicker, MultiChoicePrompt, DebugMenu, Shop, Battle, Minigame, Menu).
// Each of those widgets already ignores buttons it has no use for (e.g.
// YesNoPrompt only reacts to UP/DOWN/CONFIRM), so routing the same full
// mapping to all of them is safe and keeps the mapping itself in one place
// instead of duplicated per screen.
//
// Returns false for keys with no button mapping (e.g. the screen-specific
// hotkeys H/M/G), leaving `out` untouched.
inline bool key_to_btn(sf::Keyboard::Key code, BtnInput& out) {
    switch (code) {
    case sf::Keyboard::W: out = BTN_UP; return true;
    case sf::Keyboard::S: out = BTN_DOWN; return true;
    case sf::Keyboard::A: out = BTN_LEFT; return true;
    case sf::Keyboard::D: out = BTN_RIGHT; return true;
    case sf::Keyboard::Space:
    case sf::Keyboard::Return: out = BTN_CONFIRM; return true;
    case sf::Keyboard::BackSpace: out = BTN_CANCEL; return true;
    default: return false;
    }
}
