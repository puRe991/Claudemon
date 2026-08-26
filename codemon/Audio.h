#pragma once
#include <string>
#include <vector>
#include "SFML/Audio.hpp"

/******************************************************************************
Audio - small SFML sound manager for the imported pokeemerald audio.

The importer (tools/pe_import.py) produces SFML-loadable WAV assets:
  * assets/sfx/step.wav, bump.wav, select.wav   (short movement / UI blips)
  * assets/sfx/cries/<name>.wav                 (pokemon cries)
Music in pokeemerald is MIDI, which SFML cannot play; the importer converts it
to OGG when a synth is available. play_music() streams such a track if present.

Sound buffers must outlive the sf::Sound that plays them, so they are kept as
members here. A small ring of sf::Sound voices lets a few effects overlap.
*****************************************************************************/
class Audio
{
private:
	sf::SoundBuffer step_buf, bump_buf, select_buf;
	// Cries get their own small ring of buffers (unlike step/bump/select,
	// each cry is a *different* file loaded at play time, so two cries in
	// quick succession -- a wild encounter announces both the wild mon's and
	// the player's in the same breath -- must not reload the same buffer out
	// from under a still-playing sf::Sound that references it).
	std::vector<sf::SoundBuffer> cry_bufs;
	size_t next_cry_buf = 0;
	std::vector<sf::Sound> voices;   // simple polyphony pool
	sf::Music music;
	bool loaded;
	std::string current_bgm;   // MUS_* id currently playing ("" = none)
	bool muted = false;

	sf::Sound& free_voice();

public:
	Audio();

	// Load the standard SFX set from assets/sfx/. Returns false if the core
	// blips are missing (the game still runs, just silent).
	bool load(const std::string& asset_dir = "assets");

	bool is_loaded() const;

	// The Options screen's "Ton" (Sound) setting: SFX/cries stop playing at
	// all, and the current/future music track's volume drops to 0 -- but
	// music keeps streaming rather than stopping, so unmuting picks back up
	// mid-track instead of restarting it.
	void set_muted(bool m) { this->muted = m; this->music.setVolume(m ? 0.f : 100.f); }
	bool is_muted() const { return this->muted; }

	void play_step();
	void play_bump();
	void play_select();

	// Play a pokemon cry by name (e.g. "pikachu"); no-op if not imported.
	void play_cry(const std::string& name, const std::string& asset_dir = "assets");

	// Stream a converted OGG music track if it exists; returns success.
	bool play_music(const std::string& ogg_path, bool loop = true);

	// Higher-level wrapper around play_music() for pokeemerald's own MUS_*
	// ids (e.g. "MUS_LITTLEROOT", from Map::music()/the `playbgm` opcode):
	// resolves to assets/sfx/music/<lowercased id>.ogg and starts it looping
	// -- unless that exact track is already playing, so walking between two
	// rooms sharing music (most Pokemon Center floors, ...) doesn't restart
	// it from the top. An empty id (or one with no matching file) stops
	// whatever's currently playing, same as pokeemerald's MUS_NONE.
	void play_bgm(const std::string& mus_id, bool loop = true,
	              const std::string& asset_dir = "assets");
	void stop_music();
	const std::string& current_music() const { return this->current_bgm; }
};
