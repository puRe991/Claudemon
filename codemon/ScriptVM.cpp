#include "ScriptVM.h"
#include <cctype>

ScriptVM::ScriptVM()
	: map(nullptr), state(nullptr), box(nullptr), battle(nullptr),
	  audio(nullptr), player(nullptr), owner(nullptr), bdata(nullptr), party(nullptr),
	  st(IDLE), ip(0), switch_value(0), move_timer(0.f) {}

void ScriptVM::configure(Map* m, GameState* s, DialogBox* b, Battle* bt,
                         Audio* a, Character* p) {
	this->map = m; this->state = s; this->box = b; this->battle = bt;
	this->audio = a; this->player = p;
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
	if (s == "DIR_SOUTH") return 1;
	if (s == "DIR_NORTH") return 2;
	if (s == "DIR_WEST")  return 3;
	if (s == "DIR_EAST")  return 4;
	if (s == "VAR_FACING") return facing_to_dircode(this->player->get_facing());
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

		if (op == "msgbox" && argc >= 1) {
			this->box->open(this->owner ? std::string() : std::string(), in[1]);
			this->st = WAIT_MSG;
			return;
		} else if (op == "giveitem" && argc >= 1) {
			int amt = (argc >= 2) ? value_of(arg(1)) : 1;
			this->state->give_item(arg(0), amt);
			this->state->set_var("VAR_RESULT", 1);
			this->box->open(std::string(), item_name(arg(0)) + " obtained!");
			this->st = WAIT_MSG;
			return;
		} else if (op == "finditem" && argc >= 1) {
			// Overworld pickup (item balls / hidden items): same effect as
			// giveitem, "found" phrasing to match pokeemerald's STD_FIND_ITEM.
			int amt = (argc >= 2) ? value_of(arg(1)) : 1;
			this->state->give_item(arg(0), amt);
			this->state->set_var("VAR_RESULT", 1);
			this->box->open(std::string(), "Found " + item_name(arg(0)) + "!");
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
			if (this->battle && this->bdata && this->party &&
			    this->battle->start_trainer(tid, name_from_trainer(tid), this->party)) {
				this->st = WAIT_BATTLE;
				return;
			}
			this->box->open(std::string(), "A TRAINER wants to battle!");
			this->st = WAIT_MSG;
			return;
		} else if (op == "checkplayergender") {
			this->state->set_var("VAR_RESULT", 0);   // treat player as male
		} else if (op == "special" || op == "special2") {
			// special <Func> ; special2 <var> <Func>. Implement the few that
			// have a clear overworld effect; the rest are safely ignored.
			const std::string& fn = (op == "special2" && argc >= 2) ? arg(1) : arg(0);
			if (fn == "HealPlayerParty" && this->party) {
				this->party->hp = this->party->max_hp;
			} else if (fn == "GetPlayerXY") {
				this->state->set_var("VAR_0x8004", this->player->get_tile_x());
				this->state->set_var("VAR_0x8005", this->player->get_tile_y());
			}
			// unknown specials: no-op (cannot run arbitrary GBA C)
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
		this->st = RUN;
		this->pump();
	}
}

void ScriptVM::update(float dt) {
	if (this->st == WAIT_BATTLE) {
		if (!this->battle || !this->battle->active()) {
			// battle finished; a win-script (badge, story flag, ...) only
			// runs when the player actually won.
			bool won = this->battle && this->battle->won();
			std::string ws = this->pending_win_script;
			this->pending_win_script.clear();
			this->st = RUN;
			if (won && !ws.empty()) jump(ws);
			this->pump();
		}
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
