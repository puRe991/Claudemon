#include "Audio.h"

// A handful of voices is plenty for footsteps / bumps / warps overlapping.
Audio::Audio() {
    this->voices.resize(8);
}

bool Audio::load(const std::string& name, const std::string& path) {
    sf::SoundBuffer buffer;
    if (!buffer.loadFromFile(path)) {
        return false;
    }
    this->buffers[name] = buffer;
    return true;
}

void Audio::play(const std::string& name) {
    std::map<std::string, sf::SoundBuffer>::iterator it = this->buffers.find(name);
    if (it == this->buffers.end()) {
        return;
    }
    // Reuse the first voice that is not currently playing.
    for (sf::Sound& voice : this->voices) {
        if (voice.getStatus() != sf::Sound::Playing) {
            voice.setBuffer(it->second);
            voice.play();
            return;
        }
    }
}
