#include "Menu.h"
#include <cctype>
#include <vector>
#include <algorithm>

Menu::Menu() : font_ok(false), screen(CLOSED), cursor(0),
               bag_cursor(0), teach_cursor(0),
               gs(nullptr), bdata(nullptr), team(nullptr), box(nullptr) {}

void Menu::configure(GameState* g, std::vector<Mon>* t, std::vector<Mon>* b,
                     BattleData* bd) {
	this->gs = g; this->team = t; this->box = b; this->bdata = bd;
}

std::vector<std::pair<std::string, int>> Menu::bag_sorted() const {
	std::vector<std::pair<std::string, int>> v;
	if (this->gs)
		for (const auto& kv : this->gs->bag_items()) v.push_back(kv);
	std::sort(v.begin(), v.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	return v;
}

// Is this bag item a TM or HM? (ITEM_TM01 / ITEM_HM03)
static bool is_machine(const std::string& item) {
	return item.rfind("ITEM_TM", 0) == 0 || item.rfind("ITEM_HM", 0) == 0;
}
static bool is_hm(const std::string& item) { return item.rfind("ITEM_HM", 0) == 0; }

bool Menu::load_font(const std::string& path) {
	this->font_ok = this->font.loadFromFile(path);
	return this->font_ok;
}

static std::string pretty(const std::string& id, const std::string& prefix) {
	std::string s = id;
	if (!prefix.empty() && s.rfind(prefix, 0) == 0) s = s.substr(prefix.size());
	std::string out; bool cap = true;
	for (char c : s) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

const sf::Texture* Menu::item_icon(const std::string& item) {
	std::string f = item;
	if (f.rfind("ITEM_", 0) == 0) f = f.substr(5);
	for (char& c : f) c = (char)std::tolower((unsigned char)c);
	auto it = this->item_tex.find(f);
	if (it != this->item_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/items/" + f + ".png")) { this->item_tex[f]; return nullptr; }
	tex.setSmooth(false);
	this->item_tex[f] = tex;
	return &this->item_tex[f];
}

const sf::Texture* Menu::mon_icon(const std::string& species) {
	auto it = this->mon_tex.find(species);
	if (it != this->mon_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/pokemon/" + species + ".png")) { this->mon_tex[species]; return nullptr; }
	tex.setSmooth(false);
	this->mon_tex[species] = tex;
	return &this->mon_tex[species];
}

const sf::Texture* Menu::type_icon(const std::string& type) {
	std::string t = type;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "fighting") t = "fight";
	auto it = this->type_tex.find(t);
	if (it != this->type_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/types/" + t + ".png")) { this->type_tex[t]; return nullptr; }
	tex.setSmooth(false);
	this->type_tex[t] = tex;
	return &this->type_tex[t];
}

void Menu::open() { this->screen = MAIN; this->cursor = 0; this->flash.clear(); }
void Menu::close() { this->screen = CLOSED; }

// Teach teach_move to team[teach_cursor], consuming the TM (HMs are reusable).
void Menu::teach_selected() {
	if (!this->bdata || !this->team || this->team->empty()) return;
	if (this->teach_cursor < 0 || this->teach_cursor >= (int)this->team->size()) return;
	Mon& m = (*this->team)[this->teach_cursor];
	std::string mv_disp = pretty(this->teach_move, "");
	if (!this->bdata->can_learn_tm(m.species, this->teach_move)) {
		this->flash = pretty(m.species, "") + " can't learn " + mv_disp + ".";
		return;
	}
	if (std::find(m.moves.begin(), m.moves.end(), this->teach_move) != m.moves.end()) {
		this->flash = pretty(m.species, "") + " already knows " + mv_disp + ".";
		return;
	}
	if (m.moves.size() < 4) m.moves.push_back(this->teach_move);
	else m.moves[0] = this->teach_move;   // overwrite the oldest move
	if (!is_hm(this->teach_item) && this->gs) this->gs->take_item(this->teach_item, 1);
	this->flash = pretty(m.species, "") + " learned " + mv_disp + "!";
	this->screen = BAG;
	auto items = bag_sorted();
	if (this->bag_cursor >= (int)items.size())
		this->bag_cursor = std::max(0, (int)items.size() - 1);
}

void Menu::input(BtnInput b) {
	if (this->screen == MAIN) {
		if (b == BTN_UP && this->cursor > 0) this->cursor--;
		else if (b == BTN_DOWN && this->cursor < 4) this->cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->cursor == 0) { this->screen = BAG; this->bag_cursor = 0; this->flash.clear(); }
			else if (this->cursor == 1) this->screen = PARTY;
			else if (this->cursor == 2) this->screen = PC;
			else if (this->cursor == 3) this->screen = POKENAV;
			else this->screen = CLOSED;
		}
	} else if (this->screen == BAG) {
		auto items = bag_sorted();
		if (b == BTN_UP && this->bag_cursor > 0) this->bag_cursor--;
		else if (b == BTN_DOWN && this->bag_cursor + 1 < (int)items.size()) this->bag_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM) {
			if (this->bag_cursor < (int)items.size()) {
				const std::string& item = items[this->bag_cursor].first;
				if (is_machine(item) && this->bdata) {
					std::string tm = item.substr(5);            // ITEM_TM09 -> TM09
					std::string mv = this->bdata->tm_to_move(tm);
					if (mv.empty()) this->flash = "Nothing happens.";
					else {
						this->teach_item = item; this->teach_move = mv;
						this->teach_cursor = 0; this->flash.clear();
						this->screen = TEACH;
					}
				} else {
					this->flash = "Can't use " + pretty(item, "ITEM_") + " here.";
				}
			}
		}
	} else if (this->screen == TEACH) {
		int n = this->team ? (int)this->team->size() : 0;
		if (b == BTN_UP && this->teach_cursor > 0) this->teach_cursor--;
		else if (b == BTN_DOWN && this->teach_cursor + 1 < n) this->teach_cursor++;
		else if (b == BTN_LEFT) this->screen = BAG;
		else if (b == BTN_CONFIRM) this->teach_selected();
	} else if (b == BTN_CONFIRM || b == BTN_LEFT) {
		this->screen = MAIN;   // back from PARTY / PC / POKENAV
	}
}

void Menu::draw(sf::RenderTarget& target) {
	if (this->screen == CLOSED || !this->font_ok) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	sf::RectangleShape panel(sf::Vector2f(size.x * 0.55f, size.y - 40));
	panel.setPosition(size.x * 0.45f - 20, 20);
	panel.setFillColor(sf::Color(20, 28, 48, 240));
	panel.setOutlineColor(sf::Color::White); panel.setOutlineThickness(3.f);
	target.draw(panel);
	float x = panel.getPosition().x + 20, y = panel.getPosition().y + 18;

	auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color col) {
		sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), this->font, cs);
		t.setPosition(px, py); t.setFillColor(col); target.draw(t);
	};
	auto hp_bar = [&](float bx, float by, int hp, int mx) {
		float w = 150, r = mx > 0 ? (float)hp / mx : 0.f;
		sf::RectangleShape bg(sf::Vector2f(w, 12)); bg.setPosition(bx, by);
		bg.setFillColor(sf::Color(60, 60, 60)); target.draw(bg);
		sf::RectangleShape fg(sf::Vector2f(w * r, 12)); fg.setPosition(bx, by);
		fg.setFillColor(r > 0.5f ? sf::Color(80, 200, 80)
		               : r > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
		target.draw(fg);
	};

	if (this->screen == MAIN) {
		text("MENU", x, y, 24, sf::Color(150, 210, 255)); y += 44;
		const char* opts[] = {"BAG", "POKeMON", "PC BOX", "POKeNAV", "CLOSE"};
		for (int i = 0; i < 5; ++i) {
			bool sel = i == this->cursor;
			text((sel ? "> " : "  ") + std::string(opts[i]), x, y + i * 38, 22,
			     sel ? sf::Color(150, 210, 255) : sf::Color::White);
		}
	} else if (this->screen == BAG) {
		text("BAG", x, y, 24, sf::Color(150, 210, 255)); y += 44;
		auto items = bag_sorted();
		if (items.empty()) {
			text("(empty)", x, y, 20, sf::Color(200, 200, 200));
		} else {
			for (int row = 0; row < (int)items.size() && row < 10; ++row) {
				const auto& kv = items[row];
				bool sel = row == this->bag_cursor;
				float ry = y + row * 40;
				if (sel) {
					sf::RectangleShape hl(sf::Vector2f(panel.getSize().x - 40, 34));
					hl.setPosition(x - 6, ry - 6); hl.setFillColor(sf::Color(60, 90, 150, 120));
					target.draw(hl);
				}
				const sf::Texture* ic = item_icon(kv.first);
				if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
				bool tm = is_machine(kv.first);
				std::string label = tm ? kv.first.substr(5)               // ITEM_TM31 -> TM31
				                       : pretty(kv.first, "ITEM_");
				text(label, x + 34, ry, 20,
				     tm ? sf::Color(160, 230, 190) : sf::Color::White);
				text("x" + std::to_string(kv.second),
				     panel.getPosition().x + panel.getSize().x - 70, ry, 20, sf::Color::White);
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(255, 230, 140));
	} else if (this->screen == TEACH) {
		text("TEACH " + pretty(this->teach_move, ""), x, y, 22, sf::Color(150, 210, 255));
		y += 40;
		text("Choose a POKeMON:", x, y, 16, sf::Color(200, 200, 200)); y += 30;
		if (this->team) {
			for (int row = 0; row < (int)this->team->size() && row < 6; ++row) {
				const Mon& m = (*this->team)[row];
				bool sel = row == this->teach_cursor;
				bool able = this->bdata && this->bdata->can_learn_tm(m.species, this->teach_move);
				float ry = y + row * 44;
				if (sel) {
					sf::RectangleShape hl(sf::Vector2f(panel.getSize().x - 40, 38));
					hl.setPosition(x - 6, ry - 6); hl.setFillColor(sf::Color(60, 90, 150, 120));
					target.draw(hl);
				}
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     able ? sf::Color::White : sf::Color(150, 150, 150));
				text(able ? "ABLE" : "unable", panel.getPosition().x + panel.getSize().x - 90, ry, 16,
				     able ? sf::Color(120, 220, 120) : sf::Color(180, 120, 120));
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(255, 230, 140));
	} else if (this->screen == PARTY) {
		text("POKeMON", x, y, 24, sf::Color(150, 210, 255)); y += 40;
		if (this->team) {
			int row = 0;
			for (const Mon& m : *this->team) {
				float ry = y + row * 72;
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.9f, 0.9f); s.setPosition(x, ry); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 66, ry + 6, 20, sf::Color::White);
				hp_bar(x + 66, ry + 34, m.hp, m.max_hp);
				text(std::to_string(m.hp) + "/" + std::to_string(m.max_hp), x + 226, ry + 32, 16, sf::Color::White);
				if (++row >= 6) break;
			}
		}
	} else if (this->screen == PC) {
		text("PC BOX", x, y, 24, sf::Color(150, 210, 255)); y += 40;
		text("Stored: " + std::to_string(this->box ? (int)this->box->size() : 0), x, y, 18, sf::Color(200, 200, 200));
		y += 30;
		if (this->box && !this->box->empty()) {
			int row = 0;
			for (const Mon& m : *this->box) {
				float ry = y + row * 40;
				const sf::Texture* ic = mon_icon(m.species);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(pretty(m.species, "") + "  Lv" + std::to_string(m.level), x + 44, ry, 20, sf::Color::White);
				if (++row >= 10) break;
			}
		} else {
			text("(no POKeMON stored)", x, y, 20, sf::Color(200, 200, 200));
		}
	} else if (this->screen == POKENAV) {
		text("POKeNAV", x, y, 24, sf::Color(150, 210, 255)); y += 36;
		text("Location:  " + (this->location.empty() ? "---" : this->location),
		     x, y, 20, sf::Color(230, 230, 160)); y += 26;
		text("Region: HOENN   Party: " +
		     std::to_string(this->team ? (int)this->team->size() : 0) + "/6",
		     x, y, 16, sf::Color(200, 200, 200)); y += 30;
		text("CONDITION", x, y, 18, sf::Color(150, 210, 255)); y += 26;
		if (this->team) {
			int row = 0;
			for (const Mon& m : *this->team) {
				float ry = y + row * 58;
				text(pretty(m.species, "") + " Lv" + std::to_string(m.level), x, ry, 18, sf::Color::White);
				// type icons
				float tx2 = x + 200;
				for (const std::string& tp : {m.t1, m.t2}) {
					if (tp == m.t2 && m.t2 == m.t1) break;
					const sf::Texture* ti = type_icon(tp);
					if (ti) { sf::Sprite s(*ti); s.setPosition(tx2, ry - 2); target.draw(s); tx2 += 44; }
				}
				text("HP " + std::to_string(m.max_hp) + "  ATK " + std::to_string(m.atk) +
				     "  DEF " + std::to_string(m.def), x, ry + 24, 15, sf::Color(210, 210, 210));
				text("SPA " + std::to_string(m.spa) + "  SPD " + std::to_string(m.spd) +
				     "  SPE " + std::to_string(m.spe), x, ry + 40, 15, sf::Color(210, 210, 210));
				if (++row >= 5) break;
			}
		}
	}
	if (this->screen == BAG)
		text("[SPACE] use   [A] back", x, panel.getPosition().y + panel.getSize().y - 34, 16,
		     sf::Color(180, 180, 180));
	else if (this->screen == TEACH)
		text("[SPACE] teach   [A] back", x, panel.getPosition().y + panel.getSize().y - 34, 16,
		     sf::Color(180, 180, 180));
	else if (this->screen != MAIN)
		text("[SPACE] back", x, panel.getPosition().y + panel.getSize().y - 34, 16,
		     sf::Color(180, 180, 180));
	target.setView(saved);
}
