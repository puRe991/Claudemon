#include "Menu.h"
#include <cctype>
#include <vector>

Menu::Menu() : font_ok(false), screen(CLOSED), cursor(0),
               gs(nullptr), party(nullptr), mon_loaded(false) {}

void Menu::configure(GameState* g, Mon* p) {
	this->gs = g; this->party = p;
	if (p) {
		std::string sp = p->species;
		this->mon_loaded = this->mon_tex.loadFromFile("assets/pokemon/" + sp + ".png");
		this->mon_tex.setSmooth(false);
	}
}

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

void Menu::open() { this->screen = MAIN; this->cursor = 0; }
void Menu::close() { this->screen = CLOSED; }

void Menu::input(BtnInput b) {
	if (this->screen == MAIN) {
		if (b == BTN_UP && this->cursor > 0) this->cursor--;
		else if (b == BTN_DOWN && this->cursor < 2) this->cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->cursor == 0) this->screen = BAG;
			else if (this->cursor == 1) this->screen = PARTY;
			else this->screen = CLOSED;
		}
	} else if (this->screen == BAG || this->screen == PARTY) {
		if (b == BTN_CONFIRM) this->screen = MAIN;   // back
	}
}

void Menu::draw(sf::RenderTarget& target) {
	if (this->screen == CLOSED || !this->font_ok) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	sf::RectangleShape panel(sf::Vector2f(size.x * 0.5f, size.y - 40));
	panel.setPosition(size.x * 0.5f - 20, 20);
	panel.setFillColor(sf::Color(20, 28, 48, 240));
	panel.setOutlineColor(sf::Color::White); panel.setOutlineThickness(3.f);
	target.draw(panel);
	float x = panel.getPosition().x + 20, y = panel.getPosition().y + 18;

	auto text = [&](const std::string& s, float px, float py, unsigned cs,
	                sf::Color col) {
		sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), this->font, cs);
		t.setPosition(px, py); t.setFillColor(col); target.draw(t);
	};

	if (this->screen == MAIN) {
		text("MENU", x, y, 24, sf::Color(150, 210, 255)); y += 44;
		const char* opts[] = {"BAG", "POKeMON", "CLOSE"};
		for (int i = 0; i < 3; ++i) {
			bool sel = i == this->cursor;
			text((sel ? "> " : "  ") + std::string(opts[i]), x, y + i * 40, 22,
			     sel ? sf::Color(150, 210, 255) : sf::Color::White);
		}
	} else if (this->screen == BAG) {
		text("BAG", x, y, 24, sf::Color(150, 210, 255)); y += 44;
		if (!this->gs || this->gs->bag_items().empty()) {
			text("(empty)", x, y, 20, sf::Color(200, 200, 200));
		} else {
			int row = 0;
			for (const auto& kv : this->gs->bag_items()) {
				float ry = y + row * 40;
				const sf::Texture* ic = item_icon(kv.first);
				if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
				text(pretty(kv.first, "ITEM_"), x + 34, ry, 20, sf::Color::White);
				text("x" + std::to_string(kv.second),
				     panel.getPosition().x + panel.getSize().x - 70, ry, 20,
				     sf::Color::White);
				if (++row >= 10) break;
			}
		}
		text("[SPACE] back", x, panel.getPosition().y + panel.getSize().y - 34, 16,
		     sf::Color(180, 180, 180));
	} else if (this->screen == PARTY) {
		text("POKeMON", x, y, 24, sf::Color(150, 210, 255)); y += 40;
		if (this->party) {
			if (this->mon_loaded) {
				sf::Sprite s(this->mon_tex); s.setScale(1.6f, 1.6f);
				s.setPosition(x, y); target.draw(s);
			}
			float tx = x + 120;
			text(pretty(this->party->species, "") + "  Lv" +
			     std::to_string(this->party->level), tx, y + 8, 22, sf::Color::White);
			// HP bar
			float w = 180, hpr = this->party->max_hp > 0
			              ? (float)this->party->hp / this->party->max_hp : 0.f;
			sf::RectangleShape bg(sf::Vector2f(w, 14)); bg.setPosition(tx, y + 44);
			bg.setFillColor(sf::Color(60, 60, 60)); target.draw(bg);
			sf::RectangleShape fg(sf::Vector2f(w * hpr, 14)); fg.setPosition(tx, y + 44);
			fg.setFillColor(hpr > 0.5f ? sf::Color(80, 200, 80)
			               : hpr > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
			target.draw(fg);
			text(std::to_string(this->party->hp) + " / " +
			     std::to_string(this->party->max_hp), tx, y + 62, 18, sf::Color::White);
		}
		text("[SPACE] back", x, panel.getPosition().y + panel.getSize().y - 34, 16,
		     sf::Color(180, 180, 180));
	}
	target.setView(saved);
}
