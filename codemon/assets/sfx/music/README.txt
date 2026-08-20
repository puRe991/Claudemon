pokeemerald music is stored as MIDI (.mid). SFML cannot play MIDI.
Install fluidsynth (with a .sf2 soundfont) or timidity and re-run:
  python3 tools/pe_import.py audio --src /path/to/pokeemerald-master
to render the songs to OGG here.
