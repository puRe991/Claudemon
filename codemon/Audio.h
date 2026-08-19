#pragma once
#include <string>
#include <map>
#include <vector>

#include "SFML/Audio.hpp"

/******************************************************************************
Audio - a tiny sound-effect player for the game.

Loads named sound effects from files and plays them through a small pool of
voices so several effects can overlap. Deliberately forgiving: loading a
missing file or playing an unknown / not-yet-loaded name is a no-op, so the
game still runs on machines with no audio device or before any sounds are
supplied. Drop your own licensed audio into assets/sfx/ and load it by name.
******************************************************************************/
class Audio
{
public:
    Audio();

    // Load a sound effect from a file and remember it under `name`.
    // Returns false (and stays silent for that name) if the file is missing.
    bool load(const std::string& name, const std::string& path);

    // Play a previously-loaded effect. Unknown names are ignored.
    void play(const std::string& name);

private:
    // Node-based map: buffers stay put, so the sf::Sound pointers into them
    // stay valid while playing.
    std::map<std::string, sf::SoundBuffer> buffers;
    std::vector<sf::Sound> voices;
};
