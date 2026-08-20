#include "Audio.h"

Audio::Audio() : loaded(false) {
	// A handful of voices so step/bump/cry can overlap without cutting out.
	this->voices.resize(6);
}

sf::Sound& Audio::free_voice() {
	for (auto& v : this->voices) {
		if (v.getStatus() != sf::Sound::Playing) {
			return v;
		}
	}
	return this->voices[0];   // all busy: steal the first
}

bool Audio::load(const std::string& asset_dir) {
	const std::string sfx = asset_dir + "/sfx/";
	bool ok = true;
	ok &= this->step_buf.loadFromFile(sfx + "step.wav");
	ok &= this->bump_buf.loadFromFile(sfx + "bump.wav");
	ok &= this->select_buf.loadFromFile(sfx + "select.wav");
	this->loaded = ok;
	return ok;
}

bool Audio::is_loaded() const { return this->loaded; }

void Audio::play_step() {
	if (!this->loaded) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->step_buf);
	v.play();
}

void Audio::play_bump() {
	if (!this->loaded) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->bump_buf);
	v.play();
}

void Audio::play_select() {
	if (!this->loaded) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->select_buf);
	v.play();
}

void Audio::play_cry(const std::string& name, const std::string& asset_dir) {
	const std::string path = asset_dir + "/sfx/cries/" + name + ".wav";
	if (!this->cry_buf.loadFromFile(path)) {
		return;
	}
	sf::Sound& v = free_voice();
	v.setBuffer(this->cry_buf);
	v.play();
}

bool Audio::play_music(const std::string& ogg_path, bool loop) {
	if (!this->music.openFromFile(ogg_path)) {
		return false;
	}
	this->music.setLoop(loop);
	this->music.play();
	return true;
}
