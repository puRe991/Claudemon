#include "Audio.h"
#include <cctype>

Audio::Audio() : loaded(false) {
	// A handful of voices so step/bump/cry can overlap without cutting out.
	this->voices.resize(6);
	this->cry_bufs.resize(3);
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
	if (!this->loaded || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->step_buf);
	v.play();
}

void Audio::play_bump() {
	if (!this->loaded || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->bump_buf);
	v.play();
}

void Audio::play_select() {
	if (!this->loaded || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->select_buf);
	v.play();
}

void Audio::play_cry(const std::string& name, const std::string& asset_dir) {
	if (this->muted) return;
	const std::string path = asset_dir + "/sfx/cries/" + name + ".wav";
	sf::SoundBuffer& buf = this->cry_bufs[this->next_cry_buf];
	this->next_cry_buf = (this->next_cry_buf + 1) % this->cry_bufs.size();
	if (!buf.loadFromFile(path)) {
		return;
	}
	sf::Sound& v = free_voice();
	v.setBuffer(buf);
	v.play();
}

bool Audio::play_music(const std::string& ogg_path, bool loop) {
	if (!this->music.openFromFile(ogg_path)) {
		return false;
	}
	this->music.setLoop(loop);
	this->music.setVolume(this->muted ? 0.f : 100.f);
	this->music.play();
	return true;
}

static std::string lower(const std::string& s) {
	std::string o = s;
	for (char& c : o) c = (char)std::tolower((unsigned char)c);
	return o;
}

void Audio::play_bgm(const std::string& mus_id, bool loop, const std::string& asset_dir) {
	if (mus_id.empty() || mus_id == "MUS_NONE") { stop_music(); return; }
	if (mus_id == this->current_bgm && this->music.getStatus() == sf::Music::Playing) return;
	if (play_music(asset_dir + "/sfx/music/" + lower(mus_id) + ".ogg", loop)) {
		this->current_bgm = mus_id;
	} else {
		// Not converted (see assets/sfx/music/ -- needs fluidsynth/timidity
		// + ffmpeg, see tools/pe_import.py's cmd_audio): leave whatever was
		// playing alone rather than cutting it to silence over a missing file.
	}
}

void Audio::stop_music() {
	this->music.stop();
	this->current_bgm.clear();
}
