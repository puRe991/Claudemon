#include "Battle.h"
#include <cctype>
#include <algorithm>

Battle::Battle()
	: data(nullptr), rng(nullptr), font_ok(false), player(nullptr),
	  is_trainer(false), party_idx(0), log_pos(0), phase(INACTIVE),
	  after_msg(INACTIVE), cursor(0), over(false), victory(false),
	  has_trainer_pic(false), intro_shown(false) {}

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
	this->is_trainer = false;
	this->enemy_title.clear();
	this->party.clear(); this->party_idx = 0;
	this->enemy = this->data->make_mon(species, level);
	this->over = this->victory = false;
	this->cursor = 0;
	load_sprites();
	this->log.clear();
	queue("A wild " + nice(species) + " appeared!");
	queue("Go! " + nice(this->player->species) + "!");
	show_messages(MENU);
	return true;
}

bool Battle::start_trainer(const std::string& trainer_id, const std::string& name,
                           Mon* pm) {
	if (!this->data) return false;
	auto pty = this->data->trainer_party(trainer_id);
	if (pty.empty()) pty.push_back({"POOCHYENA", 12});   // fallback opponent
	this->player = pm;
	this->is_trainer = true;
	this->enemy_title = name.empty() ? "TRAINER" : name;
	this->party = pty; this->party_idx = 0;
	this->enemy = this->data->make_mon(pty[0].first, pty[0].second);
	this->over = this->victory = false;
	this->cursor = 0;
	// trainer front sprite for the intro
	std::string pic = this->data->trainer_pic(trainer_id);
	this->has_trainer_pic = !pic.empty() &&
		this->trainer_tex.loadFromFile("assets/trainers/" + pic + ".png");
	this->trainer_tex.setSmooth(false);
	this->intro_shown = false;
	load_sprites();
	this->log.clear();
	queue(this->enemy_title + " wants to battle!");
	queue(this->enemy_title + " sent out " + nice(this->enemy.species) + "!");
	queue("Go! " + nice(this->player->species) + "!");
	show_messages(MENU);
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

void Battle::do_move(Mon& atk, Mon& def, const std::string& mv,
                     const std::string& atk_name) {
	queue(atk_name + " used " + nice(mv) + "!");
	const MoveInfo* mi = this->data->move(mv);
	if (!mi || mi->power <= 0) return;                 // status move: no damage model
	float eff = BattleData::type_eff(mi->type, def.t1, def.t2);
	if (eff == 0.f) { queue("It doesn't affect " + nice(def.species) + "..."); return; }
	int dmg = this->data->damage(atk, def, mv, *this->rng);
	def.hp = std::max(0, def.hp - dmg);
	if (eff > 1.f) queue("It's super effective!");
	else if (eff < 1.f) queue("It's not very effective...");
	if (def.fainted()) queue(nice(def.species) + " fainted!");
}

void Battle::send_next_enemy() {
	this->party_idx++;
	this->enemy = this->data->make_mon(this->party[this->party_idx].first,
	                                   this->party[this->party_idx].second);
	load_sprites();
	queue(this->enemy_title + " sent out " + nice(this->enemy.species) + "!");
}

void Battle::resolve_turn(const std::string& player_move) {
	std::string enemy_move = ai_move();
	bool player_first = this->player->spe >= this->enemy.spe;

	Mon* first = player_first ? this->player : &this->enemy;
	Mon* second = player_first ? &this->enemy : this->player;
	std::string fm = player_first ? player_move : enemy_move;
	std::string sm = player_first ? enemy_move : player_move;
	std::string first_name  = player_first ? nice(this->player->species) : nice(this->enemy.species);
	std::string second_name = player_first ? nice(this->enemy.species) : nice(this->player->species);

	do_move(*first, *second, fm, first_name);
	if (!second->fainted() && !first->fainted())
		do_move(*second, *first, sm, second_name);

	// outcome
	if (this->player->fainted()) {
		queue(nice(this->player->species) + " fainted!");
		queue("You lost the battle...");
		this->over = true; this->victory = false;
		show_messages(INACTIVE);
		return;
	}
	if (this->enemy.fainted()) {
		if (this->is_trainer && this->party_idx + 1 < this->party.size()) {
			send_next_enemy();
			show_messages(MENU);
			return;
		}
		queue(this->is_trainer ? ("You defeated " + this->enemy_title + "!")
		                       : "The wild pokemon was defeated!");
		this->over = true; this->victory = true;
		show_messages(INACTIVE);
		return;
	}
	show_messages(MENU);
}

void Battle::input(BtnInput b) {
	if (this->phase == MSG) {
		if (b == BTN_CONFIRM) {
			this->log_pos++;
			if (this->log_pos >= this->log.size()) {
				this->log.clear();
				this->phase = this->after_msg;   // MENU or INACTIVE
				if (this->phase == MENU) this->intro_shown = true;
			}
		}
		return;
	}
	if (this->phase == MENU) {
		int n = (int)this->player->moves.size();
		if (n <= 0) return;
		if (b == BTN_LEFT  && (this->cursor % 2) == 1) this->cursor--;
		else if (b == BTN_RIGHT && (this->cursor % 2) == 0 && this->cursor + 1 < n) this->cursor++;
		else if (b == BTN_UP   && this->cursor >= 2) this->cursor -= 2;
		else if (b == BTN_DOWN && this->cursor + 2 < n) this->cursor += 2;
		else if (b == BTN_CONFIRM) {
			resolve_turn(this->player->moves[this->cursor]);
		}
	}
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
		es.setPosition(size.x * 0.62f, size.y * 0.06f);
		target.draw(es);
		if (this->font_ok) {
			sf::Text n(nice(this->enemy.species) + "  Lv" + std::to_string(this->enemy.level),
			           this->font, 20);
			n.setPosition(24, 24); n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
		}
		draw_hp_bar(target, 24, 52, 240, 16, this->enemy.hp, this->enemy.max_hp);
	}

	// player (back) bottom-left + info bottom-right
	sf::Sprite ps(this->player_tex);
	ps.setScale(2.8f, 2.8f);
	ps.setPosition(size.x * 0.10f, size.y * 0.40f);
	target.draw(ps);
	if (this->font_ok) {
		sf::Text n(nice(this->player->species) + "  Lv" + std::to_string(this->player->level),
		           this->font, 20);
		n.setPosition(size.x - 264, size.y * 0.44f);
		n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
		sf::Text hp(std::to_string(this->player->hp) + " / " + std::to_string(this->player->max_hp),
		            this->font, 18);
		hp.setPosition(size.x - 264, size.y * 0.44f + 48);
		hp.setFillColor(sf::Color(20, 20, 20)); target.draw(hp);
	}
	draw_hp_bar(target, size.x - 264, size.y * 0.44f + 28, 240, 16,
	            this->player->hp, this->player->max_hp);

	// bottom panel
	const float bh = 150.f, bm = 14.f;
	sf::RectangleShape box(sf::Vector2f(size.x - 2 * bm, bh));
	box.setPosition(bm, size.y - bh - bm);
	box.setFillColor(sf::Color(20, 28, 48, 240));
	box.setOutlineColor(sf::Color::White); box.setOutlineThickness(3.f);
	target.draw(box);
	if (!this->font_ok) { target.setView(saved); return; }

	float tx = bm + 18, ty = size.y - bh - bm + 16;
	if (this->phase == MSG) {
		std::string line = this->log_pos < this->log.size() ? this->log[this->log_pos] : "";
		sf::Text t(sf::String::fromUtf8(line.begin(), line.end()), this->font, 22);
		t.setPosition(tx, ty + 30); t.setFillColor(sf::Color::White); target.draw(t);
	} else if (this->phase == MENU) {
		sf::Text q("What will " + nice(this->player->species) + " do?", this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(sf::Color::White); target.draw(q);
		for (size_t i = 0; i < this->player->moves.size(); ++i) {
			float mx = size.x * 0.42f + (i % 2) * (size.x * 0.27f);
			float my = ty + 34 + (i / 2) * 40;
			bool sel = (int)i == this->cursor;
			sf::Text m((sel ? "> " : "  ") + nice(this->player->moves[i]), this->font, 20);
			m.setPosition(mx, my);
			m.setFillColor(sel ? sf::Color(150, 210, 255) : sf::Color::White);
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
	}
	target.setView(saved);
}
