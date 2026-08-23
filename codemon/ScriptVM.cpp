#include "ScriptVM.h"
#include <cctype>

ScriptVM::ScriptVM()
	: map(nullptr), state(nullptr), box(nullptr), battle(nullptr),
	  audio(nullptr), player(nullptr), owner(nullptr), bdata(nullptr), team(nullptr),
	  st(IDLE), ip(0), switch_value(0), move_timer(0.f) {}

void ScriptVM::configure(Map* m, GameState* s, DialogBox* b, Battle* bt,
                         Audio* a, Character* p, std::vector<Character*>* act) {
	this->map = m; this->state = s; this->box = b; this->battle = bt;
	this->audio = a; this->player = p; this->actors = act;
}

bool ScriptVM::running() const { return this->st != IDLE; }
bool ScriptVM::waiting_message() const { return this->st == WAIT_MSG; }

static std::string item_name(const std::string& item) {
	std::string s = item;
	if (s.rfind("ITEM_", 0) == 0) s = s.substr(5);
	std::string out; bool cap = true;
	for (char c : s) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

static std::string name_from_trainer(const std::string& tid) {
	std::string s = tid;
	if (s.rfind("TRAINER_", 0) == 0) s = s.substr(8);
	std::string out; bool cap = true;
	for (char c : s) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out.empty() ? "TRAINER" : out;
}

// pokeemerald DIR_* codes; our facing enum is S=0,W=1,E=2,N=3.
static int facing_to_dircode(DIR d) {
	switch (d) { case DIR::S: return 1; case DIR::N: return 2;
	             case DIR::W: return 3; case DIR::E: return 4; default: return 1; }
}

int ScriptVM::value_of(const std::string& s) const {
	if (s.empty()) return 0;
	if (s == "TRUE") return 1;
	if (s == "FALSE" || s == "NULL") return 0;
	if (s == "MALE") return 0;
	if (s == "FEMALE") return 1;
	if (s == "YES") return 1;
	if (s == "NO") return 0;
	if (s == "DIR_SOUTH") return 1;
	if (s == "DIR_NORTH") return 2;
	if (s == "DIR_WEST")  return 3;
	if (s == "DIR_EAST")  return 4;
	if (s == "VAR_FACING") return facing_to_dircode(this->player->get_facing());
	// pokeemerald's B_OUTCOME_* (include/battle.h), read back via
	// `specialvar VAR_RESULT GetBattleOutcome` after a battle.
	if (s == "B_OUTCOME_WON") return 1;
	if (s == "B_OUTCOME_LOST") return 2;
	if (s == "B_OUTCOME_DREW") return 3;
	if (s == "B_OUTCOME_RAN") return 4;
	if (s == "B_OUTCOME_PLAYER_TELEPORTED") return 5;
	if (s == "B_OUTCOME_MON_FLED") return 6;
	if (s == "B_OUTCOME_CAUGHT") return 7;
	if (s == "B_OUTCOME_NO_SAFARI_BALLS") return 8;
	if (s == "B_OUTCOME_FORFEITED") return 9;
	if (s == "B_OUTCOME_MON_TELEPORTED") return 10;
	if ((s[0] == '-' && s.size() > 1 && std::isdigit((unsigned char)s[1])) ||
	    std::isdigit((unsigned char)s[0]))
		return std::atoi(s.c_str());
	return this->state->get_var(s);      // FLAG_*/VAR_* or unknown -> stored value
}

Character* ScriptVM::resolve(const std::string& localid) const {
	if (localid.find("PLAYER") != std::string::npos) return this->player;
	return this->owner;                  // any other object -> the interacted NPC
}

void ScriptVM::start(const std::string& label, Character* npc) {
	this->owner = npc;
	this->call_stack.clear();
	this->queues.clear();
	this->move_timer = 0.f;
	this->switch_value = 0;
	if (!this->map->has_script(label)) { this->st = IDLE; return; }
	this->cur = label;
	this->ip = 0;
	this->st = RUN;
	this->pump();
}

void ScriptVM::jump(const std::string& label) {
	this->cur = label;
	this->ip = 0;
}

void ScriptVM::finish() {
	this->st = IDLE;
	this->queues.clear();
	if (this->owner) this->owner = nullptr;
}

void ScriptVM::resolve_starter(const std::string& species) {
	if (this->st != WAIT_STARTER) return;
	if (this->bdata && this->team) {
		// A fresh game's team is empty until this very moment (pokeemerald
		// never gives you a Pokemon before you actually pick one from
		// Birch's bag), so this is normally a push_back, not a replace.
		if (this->team->empty()) this->team->push_back(this->bdata->make_mon(species, 5));
		else (*this->team)[0] = this->bdata->make_mon(species, 5);
	}
	if (this->state) {
		this->state->set_flag("FLAG_SYS_POKEMON_GET");
		// pokeemerald's VAR_STARTER_MON (0=Treecko, 1=Torchic, 2=Mudkip):
		// the Route 103 rival battle switches on this to field the right
		// counter-type starter against whichever one the player picked.
		int idx = (species == "TORCHIC") ? 1 : (species == "MUDKIP") ? 2 : 0;
		this->state->set_var("VAR_STARTER_MON", idx);
	}
	this->st = RUN;
	this->pump();
}

bool ScriptVM::start_pending_wild_battle() {
	if (this->pending_wild_species.empty() || !this->battle || !this->bdata ||
	    !this->team || this->team->empty()) {
		this->pending_wild_species.clear();
		return false;
	}
	this->pending_win_script.clear();   // wild battles never run a trainerbattle win-script
	bool started = this->battle->start_wild(this->pending_wild_species,
	                                        this->pending_wild_level, &(*this->team)[0]);
	this->pending_wild_species.clear();
	if (!started) return false;
	this->st = WAIT_BATTLE;
	return true;
}

void ScriptVM::apply_move_action(Character* ch, const std::string& act) {
	if (act == "up")    ch->step(DIR::N);
	else if (act == "down")  ch->step(DIR::S);
	else if (act == "left")  ch->step(DIR::W);
	else if (act == "right") ch->step(DIR::E);
	else if (act == "face_up")    ch->face(DIR::N);
	else if (act == "face_down")  ch->face(DIR::S);
	else if (act == "face_left")  ch->face(DIR::W);
	else if (act == "face_right") ch->face(DIR::E);
	// "delay"/"end"/unknown: no positional change
}

void ScriptVM::pump() {
	// Execute instructions until we hit a blocking op or the script ends.
	int guard = 0;
	while (this->st == RUN) {
		if (++guard > 100000) { finish(); return; }   // runaway safety
		const std::vector<Instr>& code = this->map->script(this->cur);
		if (this->ip >= code.size()) { finish(); return; }
		const Instr& in = code[this->ip];
		const std::string& op = in[0];
		size_t argc = in.size() - 1;
		auto arg = [&](size_t k) -> const std::string& {
			static const std::string empty;
			return (k + 1 < in.size()) ? in[k + 1] : empty;
		};
		this->ip++;

		if ((op == "msgbox" || op == "msgboxyesno") && argc >= 1) {
			// "msgboxyesno" (see pe_import.py) is pokeemerald's
			// MSGBOX_YESNO: once the text is dismissed, the player picks
			// yes/no and the choice lands in VAR_RESULT.
			this->pending_yesno = (op == "msgboxyesno");
			this->box->open(this->owner ? std::string() : std::string(), in[1]);
			this->st = WAIT_MSG;
			return;
		} else if (op == "giveitem" && argc >= 1) {
			int amt = (argc >= 2) ? value_of(arg(1)) : 1;
			this->state->give_item(arg(0), amt);
			this->state->set_var("VAR_RESULT", 1);
			this->box->open(std::string(), item_name(arg(0)) + " erhalten!");
			this->st = WAIT_MSG;
			return;
		} else if (op == "finditem" && argc >= 1) {
			// Overworld pickup (item balls / hidden items): same effect as
			// giveitem, "found" phrasing to match pokeemerald's STD_FIND_ITEM.
			int amt = (argc >= 2) ? value_of(arg(1)) : 1;
			this->state->give_item(arg(0), amt);
			this->state->set_var("VAR_RESULT", 1);
			this->box->open(std::string(), item_name(arg(0)) + " gefunden!");
			this->st = WAIT_MSG;
			return;
		} else if (op == "applymovement" && argc >= 2) {
			Character* ch = resolve(arg(0));
			const std::vector<std::string>& acts = this->map->movement(arg(1));
			if (ch && !acts.empty()) {
				MoveQ q; q.ch = ch;
				for (const std::string& a : acts)
					if (a != "end") q.actions.push_back(a);
				this->queues.push_back(q);
			}
		} else if (op == "waitmovement") {
			bool pending = false;
			for (auto& q : this->queues) if (!q.actions.empty()) pending = true;
			if (pending) { this->st = WAIT_MOVE; return; }
		} else if (op == "faceplayer") {
			if (this->owner) {
				DIR pf = this->player->get_facing();
				DIR opp = (pf == DIR::N) ? DIR::S : (pf == DIR::S) ? DIR::N :
				          (pf == DIR::E) ? DIR::W : DIR::E;
				this->owner->face(opp);
			}
		} else if (op == "setflag" && argc >= 1) {
			this->state->set_flag(arg(0));
		} else if (op == "clearflag" && argc >= 1) {
			this->state->clear_flag(arg(0));
		} else if ((op == "setvar" || op == "setorcopyvar") && argc >= 2) {
			this->state->set_var(arg(0), value_of(arg(1)));
		} else if (op == "copyvar" && argc >= 2) {
			this->state->set_var(arg(0), value_of(arg(1)));
		} else if (op == "addvar" && argc >= 2) {
			this->state->set_var(arg(0), value_of(arg(0)) + value_of(arg(1)));
		} else if (op == "goto" && argc >= 1) {
			jump(arg(0));
		} else if (op == "goto_if_set" && argc >= 2) {
			if (this->state->flag(arg(0))) jump(arg(1));
		} else if (op == "goto_if_unset" && argc >= 2) {
			if (!this->state->flag(arg(0))) jump(arg(1));
		} else if (op == "goto_if_eq" && argc >= 3) {
			if (value_of(arg(0)) == value_of(arg(1))) jump(arg(2));
		} else if (op == "goto_if_ne" && argc >= 3) {
			if (value_of(arg(0)) != value_of(arg(1))) jump(arg(2));
		} else if (op == "call" && argc >= 1) {
			// A "call" that targets a script we don't have (most commonly a
			// shared Common_EventScript_* helper the per-map importer never
			// saw) must NOT jump into an empty body and finish() the whole
			// calling script early -- treat it as a no-op and keep going,
			// same as call returning immediately with no visible effect.
			if (this->map->has_script(arg(0))) {
				this->call_stack.push_back({this->cur, this->ip});
				jump(arg(0));
			}
		} else if (op == "call_if_set" && argc >= 2) {
			if (this->state->flag(arg(0)) && this->map->has_script(arg(1))) {
				this->call_stack.push_back({this->cur, this->ip}); jump(arg(1)); }
		} else if (op == "call_if_unset" && argc >= 2) {
			if (!this->state->flag(arg(0)) && this->map->has_script(arg(1))) {
				this->call_stack.push_back({this->cur, this->ip}); jump(arg(1)); }
		} else if (op == "call_if_eq" && argc >= 3) {
			if (value_of(arg(0)) == value_of(arg(1)) && this->map->has_script(arg(2))) {
				this->call_stack.push_back({this->cur, this->ip}); jump(arg(2)); }
		} else if (op == "return") {
			if (this->call_stack.empty()) { finish(); return; }
			auto fr = this->call_stack.back(); this->call_stack.pop_back();
			this->cur = fr.first; this->ip = fr.second;
		} else if (op == "switch" && argc >= 1) {
			this->switch_value = value_of(arg(0));
		} else if (op == "case" && argc >= 2) {
			if (this->switch_value == value_of(arg(0))) jump(arg(1));
		} else if (op == "setmetatile" && argc >= 3) {
			bool solid = (argc >= 4) ? value_of(arg(3)) != 0 : false;
			this->map->set_metatile(value_of(arg(0)), value_of(arg(1)),
			                        value_of(arg(2)), solid);
		} else if (op == "initrotatingtilepuzzle") {
			this->rot_objects.clear();
			this->rot_trick_house = argc >= 1 && value_of(arg(0)) != 0;
		} else if (op == "freerotatingtilepuzzle") {
			this->rot_objects.clear();
		} else if (op == "moverotatingtileobjects" && argc >= 1) {
			// Mossdeep Gym's rotating floor (and Trick House Puzzle #7's
			// identical mechanic): pokeemerald's own puzzle logic
			// (src/rotating_tile_puzzle.c) is entirely metatile-id-driven --
			// any character standing on one of the 4 colored arrow tiles for
			// this switch's puzzle number gets shifted one step the way its
			// arrow points, no per-map layout data needed. Both base tile
			// ids verified directly against the imported .map files (Mossdeep
			// 592-631, Trick House 664-703; 8 per color row: +0 right, +1
			// down, +2 left, +3 up).
			static const int PUZZLE_TILE_BASE = 592;         // METATILE_MossdeepGym_YellowArrow_Right
			static const int TRICKHOUSE_TILE_BASE = 664;     // METATILE_TrickHousePuzzle_Arrow_YellowOnWhite_Right
			static const char* SHIFT[4] = {"right", "down", "left", "up"};
			int base = (this->rot_trick_house ? TRICKHOUSE_TILE_BASE : PUZZLE_TILE_BASE)
			           + value_of(arg(0)) * 8;
			this->rot_objects.clear();
			if (this->actors && this->map) {
				for (Character* ch : *this->actors) {
					if (!ch) continue;
					int mt = this->map->metatile_at(ch->get_tile_x(), ch->get_tile_y());
					if (mt < base || mt >= base + 4) continue;
					MoveQ q; q.ch = ch; q.actions.push_back(SHIFT[mt - base]);
					this->queues.push_back(q);
					this->rot_objects.push_back(ch);
				}
			}
		} else if (op == "turnrotatingtileobjects") {
			// The puzzle only ever rotates counter-clockwise (pokeemerald's
			// own comment: "can only move 1 step at a time" and always CCW),
			// so any character that just shifted turns 90 degrees CCW; by
			// now they've already stepped (Character::step sets facing to
			// the walked direction), so this reads as East->Up, South->
			// Right, West->Down, North->Left -- exactly TurnRotatingTile-
			// Objects' ROTATE_COUNTERCLOCKWISE table.
			for (Character* ch : this->rot_objects) {
				if (!ch) continue;
				const char* act = nullptr;
				switch (ch->get_facing()) {
					case DIR::E: act = "face_up"; break;
					case DIR::S: act = "face_right"; break;
					case DIR::W: act = "face_down"; break;
					case DIR::N: act = "face_left"; break;
					default: break;
				}
				if (!act) continue;
				MoveQ q; q.ch = ch; q.actions.push_back(act);
				this->queues.push_back(q);
			}
		} else if (op.rfind("trainerbattle", 0) == 0) {
			// trainerbattle_single TRAINER_X, intro, lose[, winScript, flags...]
			// trainerbattle TYPE, TRAINER_X, ... -- the optional winScript label
			// (present for gym leaders/story trainers, e.g. the one that hands
			// out the badge) only runs if the player actually wins.
			std::string tid;
			this->pending_win_script.clear();
			for (size_t k = 0; k < argc; ++k) {
				const std::string& a = arg(k);
				if (a.rfind("TRAINER_", 0) == 0 && tid.empty()) tid = a;
				else if (this->pending_win_script.empty() && this->map->has_script(a))
					this->pending_win_script = a;
			}
			if (this->battle && this->bdata && this->team && !this->team->empty() &&
			    this->battle->start_trainer(tid, name_from_trainer(tid), &(*this->team)[0])) {
				this->st = WAIT_BATTLE;
				return;
			}
			this->box->open(std::string(), "Ein TRAINER möchte kämpfen!");
			this->st = WAIT_MSG;
			return;
		} else if (op == "setwildbattle" && argc >= 2) {
			// Scripted wild battles (legendaries, Kecleon, the New Mauville
			// Voltorb swarm): the species/level is set here, then a
			// following `dowildbattle` (or a legendary-specific `special`,
			// see below) actually starts it. BattleData's species table
			// keys are bare names (no SPECIES_ prefix), same as everywhere
			// else species show up in the imported scripts/encounter tables.
			std::string sp = arg(0);
			if (sp.rfind("SPECIES_", 0) == 0) sp = sp.substr(8);
			this->pending_wild_species = sp;
			this->pending_wild_level = value_of(arg(1));
		} else if (op == "dowildbattle") {
			if (start_pending_wild_battle()) return;
		} else if (op == "checkplayergender") {
			this->state->set_var("VAR_RESULT", 0);   // treat player as male
		} else if (op == "special" || op == "special2") {
			// special <Func> ; special2 <var> <Func>. Implement the few that
			// have a clear overworld effect; the rest are safely ignored.
			const std::string& fn = (op == "special2" && argc >= 2) ? arg(1) : arg(0);
			if (fn == "HealPlayerParty" && this->team) {
				for (Mon& m : *this->team) {
					m.hp = m.max_hp;
					m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
				}
				// Pokemon Center heals also mark this as the whiteout recovery
				// point (pokeemerald's lastHealLocation), read back if a later
				// battle is lost.
				if (this->state && this->map) {
					this->state->last_heal_map = this->map->name();
					this->state->last_heal_x = this->player->get_tile_x();
					this->state->last_heal_y = this->player->get_tile_y();
				}
			} else if (fn == "GetPlayerXY") {
				this->state->set_var("VAR_0x8004", this->player->get_tile_x());
				this->state->set_var("VAR_0x8005", this->player->get_tile_y());
			} else if (fn == "ChooseStarter") {
				this->st = WAIT_STARTER;
				return;
			} else if (fn == "StartRegiBattle" || fn == "BattleSetup_StartLegendaryBattle") {
				// The Regis/Rayquaza/Kyogre/Groudon scripts call one of these
				// (with special fanfare in the real game) instead of a plain
				// `dowildbattle` to start the `setwildbattle`d encounter.
				if (start_pending_wild_battle()) return;
			} else if (fn == "ScriptMenu_CreateStartMenuForPokenavTutorial") {
				// Rustboro's "here's how to use the PokeNav" tutorial opens
				// the real START menu and waits for the player to pick
				// POKeNAV, looping (via switch VAR_RESULT) on any other
				// choice. We can't drive that real menu from here, and
				// VAR_RESULT's default (0) is one of the looping cases, so
				// left alone this hangs the one-time tutorial forever and
				// with it VAR_RUSTBORO_CITY_STATE (blocking the Devon Corp
				// Pokenav hand-off and, later, the Rustboro rival battle).
				// Answer as if the player had picked POKeNAV immediately.
				this->state->set_var("VAR_RESULT", 3);
			}
			// unknown specials: no-op (cannot run arbitrary GBA C)
		} else if (op == "specialvar" && argc >= 2) {
			// specialvar VAR, Func: stores a native function's return value.
			// Implement the ones with a clear, honest answer given what this
			// engine actually models; everything else (contests, trading,
			// breeding, the fan club, Trainer Hill, ...) defaults to 0/false
			// since those systems don't exist here, so any script gated on
			// them takes the "not available" branch instead of a fake one.
			const std::string& var = arg(0);
			const std::string& fn = arg(1);
			int result = 0;
			if (fn == "GetBattleOutcome" && this->battle) {
				switch (this->battle->outcome()) {
					case Battle::OUTCOME_WON:    result = 1; break;
					case Battle::OUTCOME_LOST:   result = 2; break;
					case Battle::OUTCOME_RAN:    result = 4; break;
					case Battle::OUTCOME_CAUGHT: result = 7; break;
					default: result = 2; break;
				}
			} else if (fn == "PlayerHasBerries" && this->state) {
				for (const auto& kv : this->state->bag_items())
					if (kv.second > 0 && kv.first.find("BERRY") != std::string::npos) { result = 1; break; }
			} else if (fn == "PlayerNotAtTrainerHillEntrance") {
				result = 1;   // Trainer Hill doesn't exist here, so never "at" it
			}
			// ShouldTryRematchBattle, ShouldShowBoxWasFullMessage (our PC box
			// is a single unlimited list), GetPCBoxToSendMon (always box 0),
			// IsPokerusInParty, CountPlayerTrainerStars, and the rest: 0.
			this->state->set_var(var, result);
		} else if ((op == "warp" || op == "warpdoor" || op == "warphole" ||
		            op == "warpsilent" || op == "warpspinenter" ||
		            op == "warpteleport" || op == "warpmossdeepgym" ||
		            op == "warpwhitefade") && argc >= 1) {
			// The destination map has already changed here; nothing later in
			// this script (waitstate/release/end) still applies, so stop.
			this->warp_dest = arg(0);
			this->warp_x = (argc >= 2) ? value_of(arg(1)) : -1;
			this->warp_y = (argc >= 3) ? value_of(arg(2)) : -1;
			this->pending_warp = true;
			finish(); return;
		} else if (op == "pokemart" && argc >= 1) {
			this->pending_shop_label = arg(0);
			this->st = WAIT_SHOP;
			return;
		} else if (op == "dofieldeffect" && argc >= 1 && arg(0) == "FLDEFF_POKECENTER_HEAL") {
			// Real duration is however long CreateGlowingPokeballsEffect's
			// place -> flash -> chime sequence takes; HEALFX_DURATION below
			// mirrors that timing for the game loop's HealFx animation.
			this->healfx_timer = 0.f;
			this->st = WAIT_HEALFX;
			return;
		} else if (op == "end") {
			finish(); return;
		}
		// everything else (lock, release, playbgm, ...) is a no-op
	}
}

void ScriptVM::on_key() {
	if (this->st != WAIT_MSG) return;
	if (this->box->is_active()) { this->box->advance(); }
	if (!this->box->is_active()) {           // message fully dismissed
		if (this->battle && this->battle->active()) return;   // handled elsewhere
		if (this->pending_yesno) {
			this->pending_yesno = false;
			this->st = WAIT_YESNO;           // game loop shows the Ja/Nein choice
			return;
		}
		this->st = RUN;
		this->pump();
	}
}

void ScriptVM::resolve_yesno(bool yes) {
	if (this->st != WAIT_YESNO) return;
	if (this->state) this->state->set_var("VAR_RESULT", yes ? 1 : 0);
	this->st = RUN;
	this->pump();
}

const std::vector<std::string>* ScriptVM::shop_items() const {
	return this->map ? this->map->shop(this->pending_shop_label) : nullptr;
}

void ScriptVM::close_shop() {
	if (this->st != WAIT_SHOP) return;
	this->st = RUN;
	this->pump();
}

void ScriptVM::update(float dt) {
	if (this->st == WAIT_BATTLE) {
		if (!this->battle || !this->battle->active()) {
			bool won = this->battle && this->battle->won();
			std::string ws = this->pending_win_script;
			this->pending_win_script.clear();
			if (!won) {
				// A lost battle (trainer or wild) never resumes the calling
				// script -- on real hardware the overworld task is torn down
				// for the whiteout sequence instead. Every trainerbattle
				// script in the game (gym leaders, the Elite Four, the
				// Champion, ...) relies on this: the "you won" dialogue and
				// flag right after the battle command only runs because a
				// loss never reaches it. Match that: heal up and warp back
				// to the last place the player healed, like a real whiteout.
				if (this->team)
					for (Mon& m : *this->team) {
						m.hp = m.max_hp;
						m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
					}
				if (this->state && !this->state->last_heal_map.empty()) {
					this->warp_dest = this->state->last_heal_map;
					this->warp_x = this->state->last_heal_x;
					this->warp_y = this->state->last_heal_y;
					this->pending_warp = true;
				}
				finish();
				return;
			}
			this->st = RUN;
			if (!ws.empty()) jump(ws);
			this->pump();
		}
		return;
	}
	if (this->st == WAIT_HEALFX) {
		this->healfx_timer += dt;
		if (this->healfx_timer >= HEALFX_DURATION) { this->st = RUN; this->pump(); }
		return;
	}
	if (this->st != WAIT_MOVE) return;
	this->move_timer += dt;
	const float STEP = 0.12f;
	while (this->move_timer >= STEP) {
		this->move_timer -= STEP;
		bool any = false;
		for (auto& q : this->queues) {
			if (!q.actions.empty()) {
				apply_move_action(q.ch, q.actions.front());
				q.actions.pop_front();
				any = true;
			}
		}
		if (!any) break;
	}
	bool pending = false;
	for (auto& q : this->queues) if (!q.actions.empty()) pending = true;
	if (!pending) { this->queues.clear(); this->st = RUN; this->pump(); }
}
