#include "Battle.h"
#include <cmath>
#include <cctype>
#include <algorithm>

// Fainting cures every status condition (every generation's rule), so a
// later Revive doesn't bring a mon back still nominally poisoned/asleep/...
static void deal_damage(Mon& m, int dmg) {
	m.hp = std::max(0, m.hp - dmg);
	if (m.hp == 0) { m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0; }
}

Battle::Battle()
	: data(nullptr), rng(nullptr), gs(nullptr), team(nullptr), box(nullptr),
	  action_cursor(0), font_ok(false), player(nullptr), active_idx(0),
	  is_trainer(false), party_idx(0), switch_cursor(0), forced_switch(false),
	  log_pos(0), phase(INACTIVE),
	  after_msg(INACTIVE), cursor(0), over(false), victory(false),
	  last_outcome(OUTCOME_NONE),
	  has_trainer_pic(false), intro_shown(false),
	  shake_t(0.f), shake_side(0), prev_ehp(0), prev_php(0) {}

void Battle::set_capture(GameState* g, std::vector<Mon>* t, std::vector<Mon>* b) {
	this->gs = g; this->team = t; this->box = b;
}

void Battle::tick(float dt) {
	if (this->phase == INACTIVE) return;
	// start a shake on whichever side just lost HP
	if (this->enemy.hp < this->prev_ehp) { this->shake_t = 0.3f; this->shake_side = 1; }
	else if (this->player && this->player->hp < this->prev_php) { this->shake_t = 0.3f; this->shake_side = 2; }
	this->prev_ehp = this->enemy.hp;
	this->prev_php = this->player ? this->player->hp : 0;
	if (this->shake_t > 0.f) this->shake_t -= dt;
}

// Move type name -> type-icon file stem (pokeemerald names).
const sf::Texture* Battle::type_icon(const std::string& type) {
	std::string t = type;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "fighting") t = "fight";
	auto it = this->type_tex.find(t);
	if (it != this->type_tex.end()) return &it->second;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/types/" + t + ".png")) {
		this->type_tex[t];                 // cache the miss (empty)
		return nullptr;
	}
	tex.setSmooth(false);
	this->type_tex[t] = tex;
	return &this->type_tex[t];
}

void Battle::configure(BattleData* d, std::mt19937* r) {
	this->data = d; this->rng = r;
	this->font_ok = this->font.loadFromFile("assets/fonts/DejaVuSans.ttf");
	this->frame.load();
}

std::string Battle::nice(const std::string& id) {
	std::string out; bool cap = true;
	for (char c : id) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

void Battle::load_sprites() {
	this->enemy_tex.loadFromFile("assets/pokemon/" + this->enemy.species + ".png");
	this->enemy_tex.setSmooth(false);
	if (!this->player_tex.loadFromFile("assets/pokemon/back/" + this->player->species + ".png"))
		this->player_tex.loadFromFile("assets/pokemon/" + this->player->species + ".png");
	this->player_tex.setSmooth(false);
}

void Battle::queue(const std::string& line) { this->log.push_back(line); }

void Battle::show_messages(Phase next) {
	this->after_msg = next;
	this->log_pos = 0;
	this->phase = MSG;
}

bool Battle::start_wild(const std::string& species, int level, Mon* pm) {
	if (!this->data || !this->data->has_species(species)) return false;
	this->player = pm;
	this->active_idx = 0;
	this->forced_switch = false;
	this->is_trainer = false;
	this->enemy_title.clear();
	this->party.clear(); this->party_idx = 0;
	this->enemy = this->data->make_mon(species, level);
	this->over = this->victory = false;
	this->last_outcome = OUTCOME_NONE;
	this->cursor = 0;
	load_sprites();
	this->prev_ehp = this->enemy.hp; this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue("Wildes " + nice(species) + " taucht auf!");
	queue("Los, " + nice(this->player->species) + "!");
	show_messages(ACTION);
	return true;
}

bool Battle::start_trainer(const std::string& trainer_id, const std::string& name,
                           Mon* pm) {
	if (!this->data) return false;
	auto pty = this->data->trainer_party(trainer_id);
	if (pty.empty()) pty.push_back({"POOCHYENA", 12});   // fallback opponent
	this->player = pm;
	this->active_idx = 0;
	this->forced_switch = false;
	this->is_trainer = true;
	this->enemy_title = name.empty() ? "TRAINER" : name;
	this->party = pty; this->party_idx = 0;
	this->enemy = this->data->make_mon(pty[0].first, pty[0].second);
	this->over = this->victory = false;
	this->last_outcome = OUTCOME_NONE;
	this->cursor = 0;
	// trainer front sprite for the intro
	std::string pic = this->data->trainer_pic(trainer_id);
	this->has_trainer_pic = !pic.empty() &&
		this->trainer_tex.loadFromFile("assets/trainers/" + pic + ".png");
	this->trainer_tex.setSmooth(false);
	this->intro_shown = false;
	load_sprites();
	this->prev_ehp = this->enemy.hp; this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue(this->enemy_title + " möchte kämpfen!");
	queue(this->enemy_title + " schickt " + nice(this->enemy.species) + "!");
	queue("Los, " + nice(this->player->species) + "!");
	show_messages(ACTION);
	return true;
}

std::string Battle::ai_move() const {
	std::string best = this->enemy.moves.empty() ? "TACKLE" : this->enemy.moves[0];
	int best_dmg = -1;
	for (const std::string& m : this->enemy.moves) {
		std::mt19937 tmp(12345);
		int d = this->data->damage(this->enemy, *this->player, m, tmp);
		if (d > best_dmg) { best_dmg = d; best = m; }
	}
	return best;
}

bool Battle::roll_accuracy(int accuracy) const {
	if (accuracy <= 0) return true;   // 0 = never misses (pokeemerald convention)
	return (int)((*this->rng)() % 100) < accuracy;
}

bool Battle::status_blocks_turn(Mon& m) {
	std::string name = nice(m.species);
	if (m.status == Status::SLEEP) {
		if (--m.status_turns <= 0) {
			m.status = Status::NONE;
			queue(name + " ist aufgewacht!");
		} else {
			queue(name + " schläft tief und fest.");
			return true;
		}
	} else if (m.status == Status::FREEZE) {
		if (roll_accuracy(20)) {
			m.status = Status::NONE;
			queue(name + " ist aufgetaut!");
		} else {
			queue(name + " ist gefroren!");
			return true;
		}
	}
	if (m.confusion_turns > 0) {
		if (--m.confusion_turns <= 0) {
			queue(name + " ist nicht mehr verwirrt!");
		} else if (roll_accuracy(33)) {
			queue(name + " ist verwirrt!");
			int dmg = std::max(1, (((2 * m.level) / 5 + 2) * 40 * m.atk /
			                       std::max(1, m.def)) / 50 + 2);
			deal_damage(m, dmg);
			queue(name + " verletzt sich selbst vor Verwirrung!");
			return true;
		}
	}
	if (m.status == Status::PARALYSIS && roll_accuracy(25)) {
		queue(name + " ist paralysiert! Es kann sich nicht bewegen!");
		return true;
	}
	return false;
}

void Battle::try_inflict_status(Mon& target, const std::string& effect) {
	if (BattleData::effect_confuses(effect)) {
		if (target.confusion_turns <= 0) {
			target.confusion_turns = 2 + (int)((*this->rng)() % 3);   // 2-4 turns
			queue(nice(target.species) + " ist jetzt verwirrt!");
		}
		return;
	}
	Status st = BattleData::effect_status(effect);
	if (st == Status::NONE || target.status != Status::NONE) return;   // one major status at a time
	// Gen-3 type-based status immunities.
	if ((st == Status::POISON || st == Status::TOXIC) &&
	    (target.t1 == "POISON" || target.t2 == "POISON" ||
	     target.t1 == "STEEL" || target.t2 == "STEEL")) return;
	if (st == Status::BURN && (target.t1 == "FIRE" || target.t2 == "FIRE")) return;
	if (st == Status::FREEZE && (target.t1 == "ICE" || target.t2 == "ICE")) return;
	target.status = st;
	target.status_turns = (st == Status::SLEEP) ? 1 + (int)((*this->rng)() % 3) : 0;
	switch (st) {
		case Status::SLEEP:     queue(nice(target.species) + " ist eingeschlafen!"); break;
		case Status::POISON:    queue(nice(target.species) + " wurde vergiftet!"); break;
		case Status::TOXIC:     queue(nice(target.species) + " wurde schwer vergiftet!"); break;
		case Status::BURN:      queue(nice(target.species) + " erlitt Verbrennungen!"); break;
		case Status::PARALYSIS: queue(nice(target.species) + " ist paralysiert!"); break;
		case Status::FREEZE:    queue(nice(target.species) + " wurde eingefroren!"); break;
		default: break;
	}
}

void Battle::apply_end_of_turn_effects() {
	Mon* sides[2] = {this->player, &this->enemy};
	for (Mon* m : sides) {
		if (!m || m->fainted() || m->status == Status::NONE) continue;
		int dmg = 0;
		const char* msg = nullptr;
		if (m->status == Status::POISON) {
			dmg = std::max(1, m->max_hp / 8);
			msg = " leidet unter der Vergiftung!";
		} else if (m->status == Status::TOXIC) {
			m->status_turns++;
			dmg = std::max(1, (m->max_hp * m->status_turns) / 16);
			msg = " leidet stark unter der Vergiftung!";
		} else if (m->status == Status::BURN) {
			dmg = std::max(1, m->max_hp / 8);
			msg = " leidet unter der Verbrennung!";
		}
		if (!msg) continue;
		deal_damage(*m, dmg);
		queue(nice(m->species) + msg);
		if (m->fainted()) queue(nice(m->species) + " wurde besiegt!");
	}
}

void Battle::do_move(Mon& atk, Mon& def, const std::string& mv,
                     const std::string& atk_name) {
	if (status_blocks_turn(atk)) return;
	queue(atk_name + " setzt " + nice(mv) + " ein!");
	const MoveInfo* mi = this->data->move(mv);
	if (!mi) return;
	if (!roll_accuracy(mi->accuracy)) { queue("Der Angriff geht daneben!"); return; }
	float eff = BattleData::type_eff(mi->type, def.t1, def.t2);
	if (eff == 0.f) { queue("Hat keine Wirkung auf " + nice(def.species) + " ..."); return; }
	if (mi->power <= 0) {                      // pure status move: no damage model
		try_inflict_status(def, mi->effect);
		return;
	}
	int dmg = this->data->damage(atk, def, mv, *this->rng);
	deal_damage(def, dmg);
	if (eff > 1.f) queue("Das ist sehr effektiv!");
	else if (eff < 1.f) queue("Das ist nicht sehr effektiv ...");
	if (def.fainted()) { queue(nice(def.species) + " wurde besiegt!"); return; }
	if (mi->secondary_chance > 0 && roll_accuracy(mi->secondary_chance))
		try_inflict_status(def, mi->effect);
}

void Battle::send_next_enemy() {
	this->party_idx++;
	this->enemy = this->data->make_mon(this->party[this->party_idx].first,
	                                   this->party[this->party_idx].second);
	load_sprites();
	queue(this->enemy_title + " schickt " + nice(this->enemy.species) + "!");
}

void Battle::handle_enemy_faint() {
	// award experience to the player's pokemon
	long gain = (long)this->data->exp_yield(this->enemy.species) *
	            this->enemy.level / 7;
	std::vector<std::string> xm;
	this->data->grant_exp(*this->player, gain, xm);
	for (const std::string& m : xm) queue(m);
	if (this->is_trainer && this->party_idx + 1 < this->party.size()) {
		send_next_enemy();
		show_messages(ACTION);
		return;
	}
	queue(this->is_trainer ? ("Du hast " + this->enemy_title + " besiegt!")
	                       : "Das wilde POKéMON wurde besiegt!");
	this->over = true; this->victory = true;
	this->last_outcome = OUTCOME_WON;
	show_messages(INACTIVE);
}

void Battle::resolve_turn(const std::string& player_move) {
	std::string enemy_move = ai_move();
	// Paralysis quarters effective Speed for turn-order purposes (Gen-3).
	auto eff_speed = [](const Mon& m) {
		return m.status == Status::PARALYSIS ? std::max(1, m.spe / 4) : m.spe;
	};
	bool player_first = eff_speed(*this->player) >= eff_speed(this->enemy);

	Mon* first = player_first ? this->player : &this->enemy;
	Mon* second = player_first ? &this->enemy : this->player;
	std::string fm = player_first ? player_move : enemy_move;
	std::string sm = player_first ? enemy_move : player_move;
	std::string first_name  = player_first ? nice(this->player->species) : nice(this->enemy.species);
	std::string second_name = player_first ? nice(this->enemy.species) : nice(this->player->species);

	do_move(*first, *second, fm, first_name);
	if (!second->fainted() && !first->fainted())
		do_move(*second, *first, sm, second_name);

	apply_end_of_turn_effects();

	// outcome
	if (this->player->fainted()) {
		handle_player_faint();
		return;
	}
	if (this->enemy.fainted()) {
		handle_enemy_faint();
		return;
	}
	show_messages(ACTION);
}

void Battle::input(BtnInput b) {
	if (this->phase == MSG) {
		if (b == BTN_CONFIRM) {
			this->log_pos++;
			if (this->log_pos >= this->log.size()) {
				this->log.clear();
				this->phase = this->after_msg;   // MENU or INACTIVE
				if (this->phase == ACTION) this->intro_shown = true;
			}
		}
		return;
	}
	if (this->phase == ACTION) {
		// FIGHT / POKEMON / BALL / RUN
		if (b == BTN_UP && this->action_cursor > 0) this->action_cursor--;
		else if (b == BTN_DOWN && this->action_cursor < 3) this->action_cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->action_cursor == 0) { this->cursor = 0; this->phase = MOVE; }
			else if (this->action_cursor == 1) open_switch();
			else if (this->action_cursor == 2) throw_ball();
			else flee();
		}
		return;
	}
	if (this->phase == MOVE) {
		int n = (int)this->player->moves.size();
		if (n <= 0) return;
		if (b == BTN_LEFT  && (this->cursor % 2) == 1) this->cursor--;
		else if (b == BTN_RIGHT && (this->cursor % 2) == 0 && this->cursor + 1 < n) this->cursor++;
		else if (b == BTN_UP   && this->cursor >= 2) this->cursor -= 2;
		else if (b == BTN_DOWN && this->cursor + 2 < n) this->cursor += 2;
		else if (b == BTN_CONFIRM) resolve_turn(this->player->moves[this->cursor]);
		return;
	}
	if (this->phase == SWITCH) {
		int n = this->team ? (int)this->team->size() : 0;
		if (b == BTN_UP && this->switch_cursor > 0) this->switch_cursor--;
		else if (b == BTN_DOWN && this->switch_cursor + 1 < n) this->switch_cursor++;
		else if (b == BTN_LEFT && !this->forced_switch) this->phase = ACTION;
		else if (b == BTN_CONFIRM) do_switch(this->switch_cursor);
	}
}

bool Battle::has_healthy_reserve() const {
	if (!this->team) return false;
	for (const Mon& m : *this->team) if (!m.fainted()) return true;
	return false;
}

void Battle::handle_player_faint() {
	queue(nice(this->player->species) + " wurde besiegt!");
	if (has_healthy_reserve()) {
		this->forced_switch = true;
		this->switch_cursor = 0;
		if (this->team)
			for (size_t i = 0; i < this->team->size(); ++i)
				if (!(*this->team)[i].fainted()) { this->switch_cursor = (int)i; break; }
		show_messages(SWITCH);
	} else {
		queue("Du hast den Kampf verloren ...");
		this->over = true; this->victory = false;
		this->last_outcome = OUTCOME_LOST;
		show_messages(INACTIVE);
	}
}

void Battle::open_switch() {
	this->switch_cursor = 0;
	if (this->team)
		for (size_t i = 0; i < this->team->size(); ++i)
			if (i != this->active_idx) { this->switch_cursor = (int)i; break; }
	this->forced_switch = false;
	this->phase = SWITCH;
}

void Battle::do_switch(int idx) {
	if (!this->team || idx < 0 || idx >= (int)this->team->size()) return;
	if ((size_t)idx == this->active_idx) return;   // already out
	Mon& chosen = (*this->team)[idx];
	if (chosen.fainted()) {
		this->log.clear();
		queue(nice(chosen.species) + " kann nicht kämpfen!");
		show_messages(SWITCH);
		return;
	}
	bool was_forced = this->forced_switch;
	this->active_idx = (size_t)idx;
	this->player = &chosen;
	load_sprites();
	this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue("Los, " + nice(this->player->species) + "!");
	if (was_forced) {
		// the old mon's faint already used up this turn.
		show_messages(ACTION);
		return;
	}
	// a voluntary switch spends the turn -- the opponent gets a free hit.
	enemy_turn_after();
}

void Battle::enemy_turn_after() {
	std::string em = ai_move();
	do_move(this->enemy, *this->player, em, nice(this->enemy.species));
	apply_end_of_turn_effects();
	if (this->player->fainted()) { handle_player_faint(); return; }
	if (this->enemy.fainted()) { handle_enemy_faint(); return; }
	show_messages(ACTION);
}

void Battle::throw_ball() {
	if (this->is_trainer) {
		this->log.clear(); queue("Du kannst nicht das POKéMON eines TRAINERS fangen!");
		show_messages(ACTION); return;
	}
	if (!this->gs || this->gs->item_count("ITEM_POKE_BALL") <= 0) {
		this->log.clear(); queue("Du hast keine POKéBÄLLE mehr!");
		show_messages(ACTION); return;
	}
	this->gs->give_item("ITEM_POKE_BALL", -1);
	this->log.clear();
	queue("Du hast einen POKéBALL geworfen!");
	float ratio = this->enemy.max_hp > 0 ? 1.f - (float)this->enemy.hp / this->enemy.max_hp : 0.f;
	int chance = 35 + (int)(ratio * 55.f);        // 35%..90%, better when weakened
	if ((int)((*this->rng)() % 100) < chance) {
		queue("Erwischt! " + nice(this->enemy.species) + " wurde gefangen!");
		Mon caught = this->data->make_mon(this->enemy.species, this->enemy.level);
		if (this->team && this->team->size() < 6) this->team->push_back(caught);
		else if (this->box) { this->box->push_back(caught);
			queue(nice(this->enemy.species) + " wurde zur PC-BOX geschickt."); }
		this->over = true; this->victory = true;
		this->last_outcome = OUTCOME_CAUGHT;
		show_messages(INACTIVE);
	} else {
		queue(nice(this->enemy.species) + " hat sich befreit!");
		enemy_turn_after();
	}
}

void Battle::flee() {
	if (this->is_trainer) {
		this->log.clear(); queue("Nein! Du kannst nicht vor einem TRAINER-Kampf fliehen!");
		show_messages(ACTION); return;
	}
	this->log.clear(); queue("Erfolgreich entkommen!");
	this->over = true; this->victory = false;
	this->last_outcome = OUTCOME_RAN;
	show_messages(INACTIVE);
}

// ------------------------------------------------------------------ drawing --
static void draw_hp_bar(sf::RenderTarget& t, float x, float y, float w, float h,
                        int hp, int max_hp) {
	sf::RectangleShape bg(sf::Vector2f(w, h));
	bg.setPosition(x, y); bg.setFillColor(sf::Color(60, 60, 60));
	bg.setOutlineColor(sf::Color::White); bg.setOutlineThickness(2.f);
	t.draw(bg);
	float r = max_hp > 0 ? (float)hp / max_hp : 0.f;
	sf::RectangleShape fg(sf::Vector2f(std::max(0.f, (w - 4) * r), h - 4));
	fg.setPosition(x + 2, y + 2);
	fg.setFillColor(r > 0.5f ? sf::Color(80, 200, 80)
	               : r > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
	t.draw(fg);
}

// The player's own EXP bar (pokeemerald shows this only for the active
// player mon, never the opponent's): a thin light-blue bar under the HP bar,
// filled by progress from this level's own EXP floor to the next one's.
static void draw_exp_bar(sf::RenderTarget& t, const BattleData& bdata,
                         float x, float y, float w, float h, const Mon& mon) {
	sf::RectangleShape bg(sf::Vector2f(w, h));
	bg.setPosition(x, y); bg.setFillColor(sf::Color(60, 60, 60));
	bg.setOutlineColor(sf::Color::White); bg.setOutlineThickness(1.f);
	t.draw(bg);
	std::string growth = bdata.growth_rate(mon.species);
	long floor_now = BattleData::exp_for_level(growth, mon.level);
	long floor_next = mon.level < 100 ? BattleData::exp_for_level(growth, mon.level + 1) : floor_now;
	float r = floor_next > floor_now
	          ? (float)(mon.exp - floor_now) / (float)(floor_next - floor_now) : 1.f;
	r = std::max(0.f, std::min(1.f, r));
	sf::RectangleShape fg(sf::Vector2f(std::max(0.f, (w - 2) * r), h - 2));
	fg.setPosition(x + 1, y + 1);
	fg.setFillColor(sf::Color(88, 168, 240));
	t.draw(fg);
}

static void draw_status_badge(sf::RenderTarget& t, const sf::Font& font,
                              float x, float y, Status st) {
	const char* label = BattleData::status_name(st);
	if (!*label) return;
	sf::Color col = (st == Status::BURN) ? sf::Color(220, 100, 40)
	              : (st == Status::PARALYSIS) ? sf::Color(210, 180, 30)
	              : (st == Status::FREEZE) ? sf::Color(110, 195, 230)
	              : (st == Status::SLEEP) ? sf::Color(140, 140, 160)
	              : sf::Color(160, 70, 190);   // POISON / TOXIC
	sf::RectangleShape bg(sf::Vector2f(38, 20));
	bg.setPosition(x, y); bg.setFillColor(col);
	t.draw(bg);
	sf::Text txt(label, font, 14);
	txt.setPosition(x + 4, y + 2); txt.setFillColor(sf::Color::White);
	t.draw(txt);
}

void Battle::draw(sf::RenderTarget& target) {
	if (this->phase == INACTIVE) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	sf::RectangleShape sky(sf::Vector2f(size.x, size.y));
	sky.setFillColor(sf::Color(232, 232, 200)); target.draw(sky);
	sf::RectangleShape ground(sf::Vector2f(size.x, size.y * 0.35f));
	ground.setPosition(0, size.y * 0.45f);
	ground.setFillColor(sf::Color(200, 216, 160)); target.draw(ground);

	// hit-animation helpers: defender flashes + shakes, attacker lunges forward.
	float anim = (this->shake_t > 0.f) ? (0.3f - this->shake_t) / 0.3f : -1.f; // 0..1
	float lunge = anim >= 0.f ? std::sin(anim * 3.14159f) * 22.f : 0.f;
	bool def_enemy = this->shake_side == 1;   // enemy is the one being hit
	sf::Uint8 eflash = (def_enemy && this->shake_t > 0.f && ((int)(this->shake_t * 30) % 2)) ? 90 : 255;
	sf::Uint8 pflash = (!def_enemy && this->shake_t > 0.f && ((int)(this->shake_t * 30) % 2)) ? 90 : 255;
	float ex = (def_enemy && this->shake_t > 0.f) ? std::sin(this->shake_t * 60.f) * 8.f : 0.f;
	float ey = (!def_enemy) ? -lunge * 0.6f : 0.f;   // enemy lunges down-left when attacking
	float pxo = (!def_enemy && this->shake_t > 0.f) ? std::sin(this->shake_t * 60.f) * 8.f : 0.f;
	float pyo = (def_enemy) ? -lunge * 0.6f : 0.f;   // player lunges up-right when attacking

	// enemy side: trainer sprite during the intro, otherwise the pokemon + HP
	bool show_trainer = this->is_trainer && this->has_trainer_pic && !this->intro_shown;
	if (show_trainer) {
		sf::Sprite ts(this->trainer_tex);
		ts.setScale(2.8f, 2.8f);
		ts.setPosition(size.x * 0.60f, size.y * 0.05f);
		target.draw(ts);
	} else {
		sf::Sprite es(this->enemy_tex);
		es.setScale(2.6f, 2.6f);
		es.setColor(sf::Color(255, 255, 255, eflash));
		es.setPosition(size.x * 0.62f + ex - (def_enemy ? 0.f : lunge),
		               size.y * 0.06f + ey);
		target.draw(es);
		if (this->font_ok) {
			sf::Text n(nice(this->enemy.species) + "  Lv" + std::to_string(this->enemy.level),
			           this->font, 20);
			n.setPosition(24, 24); n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
			draw_status_badge(target, this->font, 210, 22, this->enemy.status);
		}
		draw_hp_bar(target, 24, 52, 240, 16, this->enemy.hp, this->enemy.max_hp);
	}

	// player (back) bottom-left + info bottom-right
	sf::Sprite ps(this->player_tex);
	ps.setScale(2.8f, 2.8f);
	ps.setColor(sf::Color(255, 255, 255, pflash));
	ps.setPosition(size.x * 0.10f + pxo + (def_enemy ? lunge : 0.f),
	               size.y * 0.40f + pyo);
	target.draw(ps);
	if (this->font_ok) {
		sf::Text n(nice(this->player->species) + "  Lv" + std::to_string(this->player->level),
		           this->font, 20);
		n.setPosition(size.x - 264, size.y * 0.44f);
		n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
		draw_status_badge(target, this->font, size.x - 60, size.y * 0.44f - 2, this->player->status);
		sf::Text hp(std::to_string(this->player->hp) + " / " + std::to_string(this->player->max_hp),
		            this->font, 18);
		hp.setPosition(size.x - 264, size.y * 0.44f + 48);
		hp.setFillColor(sf::Color(20, 20, 20)); target.draw(hp);
	}
	draw_hp_bar(target, size.x - 264, size.y * 0.44f + 28, 240, 16,
	            this->player->hp, this->player->max_hp);
	if (this->data)
		draw_exp_bar(target, *this->data, size.x - 264, size.y * 0.44f + 68, 240, 6,
		             *this->player);

	// bottom panel (pokeemerald's own textbox frame)
	const float bh = 150.f, bm = 14.f;
	if (this->frame.ready()) {
		this->frame.draw(target, bm, size.y - bh - bm, size.x - 2 * bm, bh, 3.f);
	} else {
		sf::RectangleShape box(sf::Vector2f(size.x - 2 * bm, bh));
		box.setPosition(bm, size.y - bh - bm);
		box.setFillColor(sf::Color(20, 28, 48, 240));
		box.setOutlineColor(sf::Color::White); box.setOutlineThickness(3.f);
		target.draw(box);
	}
	if (!this->font_ok) { target.setView(saved); return; }

	const sf::Color body_col = this->frame.ready() ? sf::Color(40, 40, 56) : sf::Color::White;
	const sf::Color head_col = this->frame.ready() ? sf::Color(24, 72, 160) : sf::Color(150, 210, 255);
	const sf::Color dis_col = this->frame.ready() ? sf::Color(170, 170, 170) : sf::Color(120, 120, 120);
	const sf::Color muted_col = this->frame.ready() ? sf::Color(100, 100, 112) : sf::Color(200, 200, 200);
	auto cursor_at = [&](float px, float py) {
		sf::Text a(">", this->font, 22); a.setPosition(px - 18, py);
		a.setFillColor(head_col); target.draw(a);
	};

	float tx = bm + 18, ty = size.y - bh - bm + 16;
	if (this->phase == MSG) {
		std::string line = this->log_pos < this->log.size() ? this->log[this->log_pos] : "";
		sf::Text t(sf::String::fromUtf8(line.begin(), line.end()), this->font, 22);
		t.setPosition(tx, ty + 30); t.setFillColor(body_col); target.draw(t);
	} else if (this->phase == ACTION) {
		sf::Text q("Was soll " + nice(this->player->species) + " tun?", this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(body_col); target.draw(q);
		static const std::string acts[] = {"KAMPF", "POKéMON", "BALL", "FLUCHT"};
		for (int i = 0; i < 4; ++i) {
			bool sel = i == this->action_cursor;
			bool dis = (i == 2 || i == 3) && this->is_trainer;   // no catching/running trainers
			if (sel) cursor_at(size.x * 0.46f, ty + i * 34);
			sf::Text a(sf::String::fromUtf8(acts[i].begin(), acts[i].end()), this->font, 22);
			a.setPosition(size.x * 0.46f, ty + i * 34);
			a.setFillColor(dis ? dis_col : sel ? head_col : body_col);
			target.draw(a);
		}
		if (this->gs) {
			std::string bt = "BÄLLE x" + std::to_string(this->gs->item_count("ITEM_POKE_BALL"));
			sf::Text bc(sf::String::fromUtf8(bt.begin(), bt.end()), this->font, 16);
			bc.setPosition(size.x * 0.72f, ty + 34);
			bc.setFillColor(muted_col); target.draw(bc);
		}
	} else if (this->phase == MOVE) {
		std::string qs = "Wähle eine Attacke:";
		sf::Text q(sf::String::fromUtf8(qs.begin(), qs.end()), this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(body_col); target.draw(q);
		for (size_t i = 0; i < this->player->moves.size(); ++i) {
			float mx = size.x * 0.42f + (i % 2) * (size.x * 0.27f);
			float my = ty + 34 + (i / 2) * 40;
			bool sel = (int)i == this->cursor;
			if (sel) cursor_at(mx, my);
			sf::Text m(nice(this->player->moves[i]), this->font, 20);
			m.setPosition(mx, my);
			m.setFillColor(sel ? head_col : body_col);
			target.draw(m);
			// type badge next to the move
			const MoveInfo* mi = this->data->move(this->player->moves[i]);
			if (mi) {
				const sf::Texture* ti = type_icon(mi->type);
				if (ti) {
					sf::Sprite badge(*ti);
					badge.setScale(1.2f, 1.2f);
					badge.setPosition(mx + size.x * 0.19f, my + 2);
					target.draw(badge);
				}
			}
		}
	} else if (this->phase == SWITCH) {
		std::string qs = this->forced_switch ? "Wer soll als nächstes kämpfen?"
		                                     : "POKéMON wählen:";
		sf::Text q(sf::String::fromUtf8(qs.begin(), qs.end()), this->font, 18);
		q.setPosition(tx, ty - 4); q.setFillColor(body_col); target.draw(q);
		if (this->team) {
			int n = std::min((int)this->team->size(), 6);
			for (int i = 0; i < n; ++i) {
				const Mon& m = (*this->team)[i];
				bool sel = i == this->switch_cursor;
				bool cur = (size_t)i == this->active_idx;
				float ry = ty + 22 + i * 18;
				if (sel) cursor_at(tx + 14, ry);
				sf::Color col = m.fainted() ? dis_col : cur ? muted_col : body_col;
				std::string label = nice(m.species) + " Lv" + std::to_string(m.level) +
				                     (cur ? " (im Kampf)" : m.fainted() ? " (K.O.)" : "");
				sf::Text t(sf::String::fromUtf8(label.begin(), label.end()), this->font, 14);
				t.setPosition(tx + 28, ry); t.setFillColor(col); target.draw(t);
				draw_hp_bar(target, size.x * 0.62f, ry + 1, 130, 10, m.hp, m.max_hp);
			}
		}
	}
	target.setView(saved);
}
