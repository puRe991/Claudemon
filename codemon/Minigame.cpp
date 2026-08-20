#include "Minigame.h"
#include <cmath>

static const char* SLOT_SYMS[] = {"7", "BAR", "@", "*", "$", "+", "o"};
static const int SLOT_N = 7;

Minigame::Minigame()
	: game(NONE), cursor(0), font_ok(false), gs(nullptr), rng(nullptr),
	  stopped(0), bet_choice(0), wheel(0), wheel_t(0.f), wheel_spin(false),
	  marker(0.f), blend_score(0), blend_presses(0),
	  rope_x(0.f), jump_y(0.f), jump_v(0.f), jump_score(0), jump_over(false) {
	reel[0] = reel[1] = reel[2] = 0; spin[0] = spin[1] = spin[2] = false;
}

void Minigame::configure(GameState* g, std::mt19937* r) { this->gs = g; this->rng = r; }
bool Minigame::load_font(const std::string& p) { return font_ok = font.loadFromFile(p); }

int Minigame::coins() const { return this->gs ? this->gs->get_var("COINS") : 0; }
void Minigame::add_coins(int n) {
	if (this->gs) this->gs->set_var("COINS", std::max(0, coins() + n));
}

void Minigame::open() { this->game = SELECT; this->cursor = 0; this->msg.clear(); }
void Minigame::close() { this->game = NONE; }

void Minigame::reset_slot() { reel[0]=reel[1]=reel[2]=0; spin[0]=spin[1]=spin[2]=false; stopped=3; msg="Press SPACE to spin (1 coin)"; }
void Minigame::reset_roulette() { bet_choice=0; wheel_spin=false; wheel_t=0; msg="Pick a colour, SPACE to bet 5"; }
void Minigame::reset_blender() { marker=0.f; blend_score=0; blend_presses=0; msg="SPACE when the marker is in the zone!"; }
void Minigame::reset_jump() { rope_x=1.f; jump_y=0.f; jump_v=0.f; jump_score=0; jump_over=false; msg="SPACE to jump the rope!"; }

void Minigame::input(BtnInput b) {
	if (this->game == SELECT) {
		if (b == BTN_UP && cursor > 0) cursor--;
		else if (b == BTN_DOWN && cursor < 4) cursor++;
		else if (b == BTN_CONFIRM) {
			if (cursor == 0) { game = SLOT; reset_slot(); }
			else if (cursor == 1) { game = ROULETTE; reset_roulette(); }
			else if (cursor == 2) { game = BLENDER; reset_blender(); }
			else if (cursor == 3) { game = JUMP; reset_jump(); }
			else game = NONE;
		}
		return;
	}
	if (this->game == SLOT) {
		if (b == BTN_CONFIRM) {
			if (stopped >= 3) {                       // start a new spin
				if (coins() <= 0) { msg = "Out of coins!"; return; }
				add_coins(-1);
				spin[0] = spin[1] = spin[2] = true; stopped = 0; msg = "Spinning...";
			} else {                                  // stop the next reel
				spin[stopped] = false; stopped++;
				if (stopped >= 3) {
					if (reel[0] == reel[1] && reel[1] == reel[2]) {
						int pay = (reel[0] == 0) ? 30 : 12;  // three 7s pays more
						add_coins(pay); msg = "JACKPOT! +" + std::to_string(pay) + " coins!";
					} else if (reel[0] == reel[1] || reel[1] == reel[2]) {
						add_coins(3); msg = "Two match! +3 coins";
					} else msg = "No match. SPACE to spin again";
				}
			}
		} else if (b == BTN_UP) game = SELECT;
		return;
	}
	if (this->game == ROULETTE) {
		if (!wheel_spin && b == BTN_LEFT && bet_choice > 0) bet_choice--;
		else if (!wheel_spin && b == BTN_RIGHT && bet_choice < 2) bet_choice++;
		else if (b == BTN_CONFIRM && !wheel_spin) {
			if (coins() < 5) { msg = "Need 5 coins!"; return; }
			add_coins(-5); wheel_spin = true; wheel_t = 1.2f; msg = "Spinning...";
		} else if (b == BTN_UP && !wheel_spin) game = SELECT;
		return;
	}
	if (this->game == BLENDER) {
		if (b == BTN_CONFIRM) {
			if (blend_presses >= 8) { game = SELECT; return; }
			float pos = 0.5f + 0.5f * std::sin(marker);
			if (pos > 0.42f && pos < 0.58f) { blend_score++; msg = "Great!"; }
			else msg = "Miss";
			if (++blend_presses >= 8) {
				add_coins(blend_score * 2);
				msg = "Blended! +" + std::to_string(blend_score * 2) + " coins (SPACE)";
			}
		} else if (b == BTN_UP) game = SELECT;
		return;
	}
	if (this->game == JUMP) {
		if (b == BTN_CONFIRM) {
			if (jump_over) { game = SELECT; return; }
			if (jump_y <= 0.01f) jump_v = 2.6f;       // jump when grounded
		} else if (b == BTN_UP) game = SELECT;
		return;
	}
}

void Minigame::tick(float dt) {
	if (this->game == SLOT) {
		for (int i = 0; i < 3; ++i)
			if (spin[i]) reel[i] = (int)((*rng)() % SLOT_N);
	} else if (this->game == ROULETTE) {
		if (wheel_spin) {
			wheel_t -= dt;
			wheel = (int)((*rng)() % 3);
			if (wheel_t <= 0.f) {
				wheel_spin = false;
				if (wheel == bet_choice) { add_coins(15); msg = "You won! +15 coins"; }
				else msg = "You lost. SPACE to bet again";
			}
		}
	} else if (this->game == BLENDER) {
		if (blend_presses < 8) marker += dt * 3.2f;    // phase; marker pos = 0.5+0.5 sin
	} else if (this->game == JUMP) {
		if (!jump_over) {
			if (jump_v > 0.f || jump_y > 0.f) { jump_y += jump_v * dt * 3.f; jump_v -= 9.f * dt; if (jump_y < 0) { jump_y = 0; jump_v = 0; } }
			rope_x -= dt * 0.9f;
			if (rope_x < 0.12f && rope_x > 0.04f && jump_y < 0.25f) {
				jump_over = true; add_coins(jump_score);
				msg = "Tripped! +" + std::to_string(jump_score) + " coins (SPACE)";
			} else if (rope_x <= 0.f) { rope_x = 1.f; jump_score++; msg = "Score: " + std::to_string(jump_score); }
		}
	}
}

void Minigame::draw(sf::RenderTarget& target) {
	if (this->game == NONE || !font_ok) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f sz = target.getView().getSize();

	sf::RectangleShape bg(sz); bg.setFillColor(sf::Color(30, 20, 40)); target.draw(bg);
	auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color c) {
		sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, cs);
		t.setPosition(px, py); t.setFillColor(c); target.draw(t);
	};
	text("COINS: " + std::to_string(coins()), sz.x - 200, 20, 22, sf::Color(255, 220, 90));

	if (this->game == SELECT) {
		text("GAME CORNER", 60, 50, 30, sf::Color(150, 210, 255));
		const char* g[] = {"SLOT MACHINE", "ROULETTE", "BERRY BLENDER", "POKeMON JUMP", "EXIT"};
		for (int i = 0; i < 5; ++i)
			text((i == cursor ? "> " : "  ") + std::string(g[i]), 80, 120 + i * 46, 24,
			     i == cursor ? sf::Color(150, 210, 255) : sf::Color::White);
		return;
	}
	text("[UP] leave", sz.x - 200, 50, 16, sf::Color(180, 180, 180));

	if (this->game == SLOT) {
		text("SLOT MACHINE", 60, 40, 26, sf::Color(150, 210, 255));
		for (int i = 0; i < 3; ++i) {
			sf::RectangleShape r(sf::Vector2f(120, 120));
			r.setPosition(120 + i * 150, 140);
			r.setFillColor(sf::Color(240, 240, 220));
			r.setOutlineColor(spin[i] ? sf::Color(255, 200, 80) : sf::Color(120, 120, 120));
			r.setOutlineThickness(4); target.draw(r);
			text(SLOT_SYMS[reel[i]], 160 + i * 150, 170, 48, sf::Color(40, 40, 40));
		}
		text(msg, 60, 300, 20, sf::Color::White);
		text(stopped >= 3 ? "SPACE: spin" : "SPACE: stop reel", 60, 340, 18, sf::Color(200, 200, 200));
	} else if (this->game == ROULETTE) {
		text("ROULETTE", 60, 40, 26, sf::Color(150, 210, 255));
		const char* cols[] = {"RED", "GREEN", "BLUE"};
		sf::Color cc[] = {sf::Color(220, 70, 70), sf::Color(80, 200, 80), sf::Color(90, 130, 240)};
		for (int i = 0; i < 3; ++i) {
			sf::RectangleShape r(sf::Vector2f(130, 90)); r.setPosition(90 + i * 160, 150);
			r.setFillColor(cc[i]);
			r.setOutlineColor((wheel_spin ? wheel : bet_choice) == i ? sf::Color::White : sf::Color(60, 60, 60));
			r.setOutlineThickness((wheel_spin ? wheel : bet_choice) == i ? 5 : 2);
			target.draw(r);
			text(cols[i], 120 + i * 160, 180, 22, sf::Color::White);
		}
		text(msg, 60, 300, 20, sf::Color::White);
	} else if (this->game == BLENDER) {
		text("BERRY BLENDER", 60, 40, 26, sf::Color(150, 210, 255));
		sf::RectangleShape bar(sf::Vector2f(500, 30)); bar.setPosition(80, 180);
		bar.setFillColor(sf::Color(60, 60, 60)); target.draw(bar);
		sf::RectangleShape zone(sf::Vector2f(500 * 0.16f, 30)); zone.setPosition(80 + 500 * 0.42f, 180);
		zone.setFillColor(sf::Color(80, 200, 80)); target.draw(zone);
		float mpos = 0.5f + 0.5f * std::sin(marker);
		sf::RectangleShape mk(sf::Vector2f(8, 40)); mk.setPosition(80 + 500 * mpos - 4, 175);
		mk.setFillColor(sf::Color(255, 220, 90)); target.draw(mk);
		text("Presses: " + std::to_string(blend_presses) + "/8   Good: " + std::to_string(blend_score),
		     80, 240, 20, sf::Color::White);
		text(msg, 80, 280, 20, sf::Color::White);
	} else if (this->game == JUMP) {
		text("POKeMON JUMP", 60, 40, 26, sf::Color(150, 210, 255));
		sf::RectangleShape ground(sf::Vector2f(600, 6)); ground.setPosition(60, 300);
		ground.setFillColor(sf::Color(180, 180, 180)); target.draw(ground);
		sf::CircleShape p(18); p.setFillColor(sf::Color(90, 200, 120));
		p.setPosition(120, 300 - 36 - jump_y * 180); target.draw(p);
		sf::RectangleShape rope(sf::Vector2f(10, 30)); rope.setPosition(60 + rope_x * 600, 272);
		rope.setFillColor(sf::Color(220, 120, 80)); target.draw(rope);
		text("Score: " + std::to_string(jump_score), 60, 340, 20, sf::Color::White);
		text(msg, 60, 370, 18, sf::Color(220, 220, 220));
	}
	target.setView(saved);
}
