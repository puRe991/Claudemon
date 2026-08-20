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
	sf::SoundBuffer step_buf, bump_buf, select_buf, cry_buf;
	std::vector<sf::Sound> voices;   // simple polyphony pool
	sf::Music music;
	bool loaded;

	sf::Sound& free_voice();

public:
	Audio();

	// Load the standard SFX set from assets/sfx/. Returns false if the core
	// blips are missing (the game still runs, just silent).
	bool load(const std::string& asset_dir = "assets");

	bool is_loaded() const;

	void play_step();
	void play_bump();
	void play_select();

	// Play a pokemon cry by name (e.g. "pikachu"); no-op if not imported.
	void play_cry(const std::string& name, const std::string& asset_dir = "assets");

	// Stream a converted OGG music track if it exists; returns success.
	bool play_music(const std::string& ogg_path, bool loop = true);
};
