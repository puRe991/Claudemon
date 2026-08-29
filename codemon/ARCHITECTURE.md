# Architektur-Übersicht

Kurzer Einstieg für alle, die sich neu in die Engine einarbeiten. Für Details siehe die Header-Kommentare der jeweiligen Klasse (jede zentrale Klasse trägt einen `/**** Name - Beschreibung ****/`-Block direkt am Dateianfang) sowie `tools/README.md` (Import-Pipeline) und `region/README.md` (Weltdaten).

## Grobstruktur

```
codemon/
├── main.cpp          Game-Klasse + freie Hilfsfunktionen (Session/Kollision/Warps)
├── Game (in main.cpp)  Eigentümer der Session, aller UI-Screens, der Game-Loop
├── ScriptVM.*        Interpreter für importierte pokeemerald-Event-Skripte
├── Battle.*           Rundenbasiertes Kampfsystem
├── BattleData.*        Statische Kampfdaten (Arten, Attacken, Lernsets, Typtabelle)
├── Menu.*, Minigame.*   Overworld-Menü (Beutel/Team/PC/PokéNav) und Minispiele
├── StarterSelect.h, YesNoPrompt.h, PartyPicker.h, ...   Einzelne UI-Screens (je eine Datei)
├── InputRouter.h        WASD/Space/Enter/Backspace -> BtnInput, zentral an einer Stelle
├── map.*, TileMap.*, Tileset.*, Tile.*   Kartendaten + Rendering
├── character.*         Spieler-/NPC-Sprite, Bewegung, Animation
├── Region.*            Verzeichnis der Gebiete (region/kanto.region) + Verbindungsgraph
├── GameState.h          Persistenter Zustand: Flags/Variablen/Beutel/Geld
├── SaveGame.*           Serialisiert GameState + Team + PC-Box in savegame.dat
├── Audio.*, DialogBox.*, UiFrame.h   Sound, Textbox, das gemeinsame 9-Slice-Fensterrahmen-Widget
├── tools/pe_import.py   Import-Pipeline: pokeemerald-Checkout -> assets/, maps/
├── region/               Handgepflegte Zusatz-Weltkarte (region/README.md)
├── editor/               Eigenständiger Karteneditor (editor/README.md)
└── tests/                codemon_tests (reine Logik) + codemon_engine_tests (Skripte/Kämpfe end-to-end)
```

## Kernidee: datengetrieben statt hartkodiert

Nichts über Pokémon, Karten oder Kämpfe steht im C++-Code selbst. `tools/pe_import.py` liest einen echten **pokeemerald**-Checkout und erzeugt daraus:

* `assets/tilesets/*.png`, `assets/overworld/*.png`, `assets/pokemon/*.png` – fertig eingefärbte Grafiken
* `codemon/maps/*.map` – eine Datei pro Karte: Metatiles, Kollision, Warps, NPCs, Skripte, Wildbegegnungen
* `codemon/assets/battle/*.tsv` – Arten, Attacken, Lernsets, Trainerteams, Entwicklungen

Die Engine liest diese Dateien zur Laufzeit; ein erneuter Import-Lauf reicht, um Daten zu aktualisieren, ohne C++ anzufassen. Details: `tools/README.md`.

## Der zentrale Ablauf: `Game` (main.cpp)

`main()` ist nur `Game game; return game.run();`. Die `Game`-Klasse hält den gesamten Laufzeitzustand als Member (siehe main.cpp, Klassendefinition oben in der Datei) und läuft in zwei Modi:

* **`run_interactive()`** – die echte SFML-Fensterschleife: Events lesen → `InputRouter::key_to_btn()` übersetzt Tasten in `BtnInput` → an den gerade aktiven UI-Screen weiterreichen (Battle, Menu, ein Prompt, ...) oder, wenn nichts blockiert, den Spieler bewegen → `update()`/`tick()` auf allen Subsystemen → zeichnen.
* **`run_headless()`** – dieselbe Zustandsmaschine, aber token-getrieben (`CODEMON_WALK=N,N,T,...`) und ohne echtes Fenster; rendert stattdessen `CODEMON_SCREENSHOT`-PNGs. Das ist die Grundlage für die Screenshot-Regressionstests, gegen die jedes Refactoring in diesem Repo verifiziert wird.

Eine **Session** (`struct Session` in main.cpp) bündelt alles, was an der aktuell geladenen Karte hängt: `Map*`, den Spieler-`Character*`, alle NPC-`Agent`s/`Character*`. Ein Kartenwechsel (Warp, Fliegen, Kartenverbindung, Whiteout) tauscht die `Session` komplett aus (`load_session()` / `free_session()`), nie die Karte in-place.

## Skripte: `ScriptVM`

Jede NPC-Interaktion, jeder Story-Trigger läuft über `ScriptVM::pump()` – ein kooperativer Interpreter für die importierten pokeemerald-Opcodes (msgbox, applymovement, goto/call, trainerbattle, special, ...). Er läuft bis zu einem blockierenden Opcode (Textbox, Bewegung, Kampf) und pausiert dann; `Game` weckt ihn wieder, sobald der Spieler eine Nachricht bestätigt oder eine Bewegung fertig ist. Der Opcode-Dispatch selbst ist in acht thematische `try_*_op()`-Methoden aufgeteilt (siehe README-Abschnitt „Code-Qualität / Engine-Wartbarkeit").

## Kampf: `Battle` + `BattleData`

`BattleData` lädt die reinen Tabellen (Arten, Attacken, Typchart, Lernsets) und kennt keine Laufzeit-UI. `Battle` besitzt eine laufende Auseinandersetzung (wild oder Trainer), wertet Züge nach der `BattleData`-Formel aus und zeichnet HP-Balken/Logfenster/Aktionsmenü. `ScriptVM::try_battle_op()` startet Trainerkämpfe aus Skripten heraus; `Game` startet wilde Begegnungen direkt beim Betreten von Gras-Tiles.

## UI-Screens

Jeder Bildschirm (Titelscreen, Namenseingabe, Shop, Debug-Menü, Ja/Nein-Prompt, ...) ist eine eigene, kleine Klasse mit demselben Grundmuster: `load()`, `active()`, `input(BtnInput)`, `draw(target)`. `Game` hält eine Instanz von jedem und fragt in `run_interactive()`/`run_headless()` reihum ab, welcher gerade `active()` ist. `Menu` (das In-Game-Pausenmenü mit Beutel/Team/PC/PokéNav) folgt demselben Muster, ist aber selbst wieder in `draw_<screen>()`-Methoden pro Unterbildschirm aufgeteilt.

## Persistenz

`GameState` ist der eine Ort für alles, was über einen Kartenwechsel hinweg gilt (Flags, Variablen, Beutel, Geld) – pokeemerald-Skripte lesen/schreiben genau diese Flags/Variablen. `SaveGame` serialisiert `GameState` + Team + PC-Box + aktuelle Karte/Position in eine einzige lesbare Textdatei.

## Build & Tests

CMake baut eine gemeinsame statische Lib `monsta_engine` (alle Engine-Quellen außer `main.cpp`) und drei Executables darauf: `codemon` (das Spiel), `codemon_editor`/`codemon_editor_imgui` (Karteneditor) und zwei Testbinaries:

* `codemon_tests` – reine Logik ohne Display (Coordinates, Tile, ...)
* `codemon_engine_tests` – lässt echte importierte Skripte/Kämpfe gegen Fixture-Maps laufen (braucht ein Display bzw. Xvfb) und ist der wichtigste Sicherheitsnetz für Änderungen an `ScriptVM`/`Battle`

Der aktuelle Stand laufender Wartbarkeits-Refactorings (was schon aufgeteilt wurde, was noch aussteht) steht im README-Abschnitt „🔧 Code-Qualität / Engine-Wartbarkeit".
