#include "Audio.h"
#include <SFML/System/Err.hpp>
#include <cctype>

Audio::Audio() : loaded(false) {
	// A missing/broken audio device makes SFML log a multi-line OpenAL error
	// to sf::err() (std::cerr) on every single low-level audio call it makes
	// -- and a stuck sf::Music keeps making those calls from its own
	// background streaming thread indefinitely (see play_music()'s
	// device_ok check), which turns into gigabytes of log output and real
	// CPU/IO overhead, not just noise. Silencing sf::err() is SFML's own
	// documented way to drop this: writes to a null streambuf are no-ops.
	sf::err().rdbuf(nullptr);
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
	if (!this->loaded || !this->device_ok || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->step_buf);
	v.play();
}

void Audio::play_bump() {
	if (!this->loaded || !this->device_ok || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->bump_buf);
	v.play();
}

void Audio::play_select() {
	if (!this->loaded || !this->device_ok || this->muted) return;
	sf::Sound& v = free_voice();
	v.setBuffer(this->select_buf);
	v.play();
}

void Audio::play_cry(const std::string& name, const std::string& asset_dir) {
	// Same "no usable audio device" proxy play_step/play_bump/play_select
	// already gate on (see load()) -- without it, a broken/missing device
	// makes every loadFromFile() fail via OpenAL, and unlike those three
	// fixed buffers, a cry reloads its buffer on every single call, so nothing
	// here was skipping the repeated failing calls (and the SFML error log
	// each one prints) at all.
	if (!this->loaded || !this->device_ok || this->muted) return;
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
	if (!this->device_ok) return false;
	if (!this->music.openFromFile(ogg_path)) {
		return false;
	}
	this->music.setLoop(loop);
	this->music.setVolume(this->muted ? 0.f : 100.f);
	this->music.play();
	// loadFromFile()/openFromFile() only decode the file; if the OpenAL
	// device itself is unavailable, play() issues the request but the
	// stream never actually starts (getStatus() stays Stopped) -- SFML logs
	// the low-level OpenAL failure instead of surfacing it here, and (worse)
	// leaves this Music's background streaming thread spinning against a
	// broken source forever. Treat "still Stopped right after play()" as
	// proof the device is gone and stop asking it to do anything else.
	if (this->music.getStatus() != sf::Music::Playing) {
		this->music.stop();
		this->device_ok = false;
		return false;
	}
	return true;
}

static std::string lower(const std::string& s) {
	std::string o = s;
	for (char& c : o) c = (char)std::tolower((unsigned char)c);
	return o;
}

void Audio::play_bgm(const std::string& mus_id, bool loop, const std::string& asset_dir) {
	if (mus_id.empty() || mus_id == "MUS_NONE") { stop_music(); return; }
	// No usable audio device at all (see load()): sf::Music::openFromFile
	// would only fail via OpenAL same as every SoundBuffer does, so don't
	// even try -- avoids hammering a broken device with retries every time
	// a map/script asks for music.
	if (!this->loaded || !this->device_ok) return;
	if (mus_id == this->current_bgm && this->music.getStatus() == sf::Music::Playing) return;
	// Already tried this exact id and it had no .ogg -- don't hammer
	// openFromFile() (and SFML's error log) again for the same miss.
	if (mus_id == this->last_attempted_bgm && mus_id != this->current_bgm) return;
	this->last_attempted_bgm = mus_id;
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
