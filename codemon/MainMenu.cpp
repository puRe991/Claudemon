#include "MainMenu.h"

#include <cmath>

/* ---- palette ------------------------------------------------------------ */
namespace {
	const sf::Color kBgTop     (24, 40, 96);
	const sf::Color kBgBottom  (8, 16, 48);
	const sf::Color kTitle     (248, 208, 48);
	const sf::Color kTitleShade(120, 40, 8);
	const sf::Color kSubtitle  (208, 216, 248);
	const sf::Color kBoxFill   (248, 248, 248);
	const sf::Color kBoxBorder (40, 40, 40);
	const sf::Color kText      (40, 40, 40);
	const sf::Color kTextOff   (176, 176, 176);
	const sf::Color kCursor    (200, 32, 32);

	// Build a text object with the font/size/colour set, origin at top-left.
	sf::Text make_text(const sf::Font& font, const std::string& s,
	                   unsigned int size, sf::Color color) {
		sf::Text t;
		t.setFont(font);
		t.setString(s);
		t.setCharacterSize(size);
		t.setFillColor(color);
		return t;
	}
}

MainMenu::MainMenu(float virtual_w, float virtual_h)
	: m_vw(virtual_w), m_vh(virtual_h),
	  m_loaded(false), m_screen(Screen::Title), m_time(0.f)
{
	// "Continue" is disabled until there is a save system to load from.
	this->m_menu.add_item("NEUES SPIEL", MenuAction::NewGame,  true);
	this->m_menu.add_item("FORTSETZEN",  MenuAction::Continue, false);
	this->m_menu.add_item("OPTIONEN",    MenuAction::Options,  true);
	this->m_menu.add_item("BEENDEN",     MenuAction::Quit,     true);
}

bool MainMenu::load(const std::string& font_path)
{
	this->m_loaded = this->m_font.loadFromFile(font_path);
	return this->m_loaded;
}

void MainMenu::update(float dt)
{
	this->m_time += dt;
}

MenuResult MainMenu::handle_key(sf::Keyboard::Key key)
{
	switch (this->m_screen) {

	case Screen::Title:
		if (key == sf::Keyboard::Enter || key == sf::Keyboard::Space) {
			this->m_screen = Screen::Menu;
		} else if (key == sf::Keyboard::Escape) {
			return MenuResult::Quit;
		}
		break;

	case Screen::Menu:
		if (key == sf::Keyboard::Up || key == sf::Keyboard::W) {
			this->m_menu.move_up();
		} else if (key == sf::Keyboard::Down || key == sf::Keyboard::S) {
			this->m_menu.move_down();
		} else if (key == sf::Keyboard::Enter || key == sf::Keyboard::Space) {
			switch (this->m_menu.confirm()) {
			case MenuAction::NewGame:  return MenuResult::StartGame;
			case MenuAction::Options:  this->m_screen = Screen::Options; break;
			case MenuAction::Quit:     return MenuResult::Quit;
			default: break; // Continue/None: nothing to do
			}
		} else if (key == sf::Keyboard::Escape) {
			this->m_screen = Screen::Title;
		}
		break;

	case Screen::Options:
		if (key == sf::Keyboard::Escape || key == sf::Keyboard::Enter || key == sf::Keyboard::Space) {
			this->m_screen = Screen::Menu;
		}
		break;
	}

	return MenuResult::None;
}

/* ---- rendering ---------------------------------------------------------- */

void MainMenu::render(sf::RenderTarget& target)
{
	this->draw_background(target);

	if (!this->m_loaded) {
		return; // no font: at least clear to the background colour
	}

	switch (this->m_screen) {
	case Screen::Title:   this->draw_title_screen(target);   break;
	case Screen::Menu:    this->draw_menu_screen(target);    break;
	case Screen::Options: this->draw_options_screen(target); break;
	}
}

void MainMenu::draw_background(sf::RenderTarget& target)
{
	sf::VertexArray bg(sf::Quads, 4);
	bg[0] = sf::Vertex(sf::Vector2f(0.f,     0.f),     kBgTop);
	bg[1] = sf::Vertex(sf::Vector2f(this->m_vw, 0.f),  kBgTop);
	bg[2] = sf::Vertex(sf::Vector2f(this->m_vw, this->m_vh), kBgBottom);
	bg[3] = sf::Vertex(sf::Vector2f(0.f,     this->m_vh), kBgBottom);
	target.draw(bg);
}

void MainMenu::draw_text(sf::RenderTarget& target, const std::string& s, unsigned int size,
                         float cx, float cy, sf::Color color, bool shadow)
{
	sf::Text t = make_text(this->m_font, s, size, color);
	sf::FloatRect b = t.getLocalBounds();
	t.setOrigin(b.left + b.width / 2.f, b.top + b.height / 2.f);

	if (shadow) {
		sf::Text sh = t;
		sh.setFillColor(kTitleShade);
		sh.setPosition(cx + size * 0.06f + 2.f, cy + size * 0.06f + 2.f);
		target.draw(sh);
	}

	t.setPosition(cx, cy);
	target.draw(t);
}

// Draw a simple Poke-ball-ish emblem centred at (cx, cy).
static void draw_pokeball(sf::RenderTarget& target, float cx, float cy, float r)
{
	sf::CircleShape ball(r);
	ball.setOrigin(r, r);
	ball.setPosition(cx, cy);
	ball.setFillColor(sf::Color(248, 248, 248));
	ball.setOutlineThickness(3.f);
	ball.setOutlineColor(sf::Color(40, 40, 40));
	target.draw(ball);

	sf::RectangleShape band(sf::Vector2f(r * 2.f, r * 0.28f));
	band.setOrigin(r, band.getSize().y / 2.f);
	band.setPosition(cx, cy);
	band.setFillColor(sf::Color(40, 40, 40));
	target.draw(band);

	sf::CircleShape button(r * 0.30f);
	button.setOrigin(r * 0.30f, r * 0.30f);
	button.setPosition(cx, cy);
	button.setFillColor(sf::Color(248, 248, 248));
	button.setOutlineThickness(3.f);
	button.setOutlineColor(sf::Color(40, 40, 40));
	target.draw(button);
}

void MainMenu::draw_title_screen(sf::RenderTarget& target)
{
	const float cx = this->m_vw / 2.f;

	draw_pokeball(target, cx, this->m_vh * 0.18f, this->m_vh * 0.09f);

	this->draw_text(target, "CODEMON", 40, cx, this->m_vh * 0.40f, kTitle, true);
	this->draw_text(target, "EIN MONSTA-ENGINE-SPIEL", 10, cx, this->m_vh * 0.40f + 44.f, kSubtitle);

	// Blinking prompt: on for ~0.6s, off for ~0.4s.
	if (std::fmod(this->m_time, 1.0f) < 0.6f) {
		this->draw_text(target, "ENTER DRÜCKEN", 14, cx, this->m_vh * 0.78f, kSubtitle);
	}
}

void MainMenu::draw_menu_screen(sf::RenderTarget& target)
{
	const float cx = this->m_vw / 2.f;

	this->draw_text(target, "CODEMON", 24, cx, this->m_vh * 0.15f, kTitle, true);

	const std::vector<MenuModel::Item>& items = this->m_menu.items();

	// Menu box.
	const float box_w = 240.f;
	const float item_h = 44.f;
	const float box_h = items.size() * item_h + 32.f;
	const float box_x = cx - box_w / 2.f;
	const float box_y = this->m_vh * 0.30f;

	sf::RectangleShape box(sf::Vector2f(box_w, box_h));
	box.setPosition(box_x, box_y);
	box.setFillColor(kBoxFill);
	box.setOutlineThickness(4.f);
	box.setOutlineColor(kBoxBorder);
	target.draw(box);

	const float text_x = box_x + 52.f;
	const float first_y = box_y + 16.f + item_h / 2.f;

	for (unsigned int i = 0; i < items.size(); ++i) {
		const MenuModel::Item& item = items[i];
		const float y = first_y + i * item_h;

		sf::Text label = make_text(this->m_font, item.label, 16,
		                           item.enabled ? kText : kTextOff);
		sf::FloatRect b = label.getLocalBounds();
		label.setOrigin(0.f, b.top + b.height / 2.f);
		label.setPosition(text_x, y);
		target.draw(label);

		if (i == this->m_menu.selected()) {
			const float bob = std::sin(this->m_time * 8.f) * 3.f;
			sf::Text cursor = make_text(this->m_font, ">", 16, kCursor);
			sf::FloatRect cb = cursor.getLocalBounds();
			cursor.setOrigin(0.f, cb.top + cb.height / 2.f);
			cursor.setPosition(box_x + 22.f + bob, y);
			target.draw(cursor);
		}
	}
}

void MainMenu::draw_options_screen(sf::RenderTarget& target)
{
	const float cx = this->m_vw / 2.f;

	this->draw_text(target, "STEUERUNG", 24, cx, this->m_vh * 0.13f, kTitle, true);

	struct Row { const char* left; const char* right; };
	const Row rows[] = {
		{ "BEWEGEN",    "W A S D" },
		{ "BESTÄTIGEN", "ENTER"   },
		{ "ZURÜCK",     "ESC"     },
		{ "VOLLBILD",   "F11"     },
	};
	const int row_count = sizeof(rows) / sizeof(rows[0]);

	const float box_w = 320.f;
	const float row_h = 40.f;
	const float box_h = row_count * row_h + 32.f;
	const float box_x = cx - box_w / 2.f;
	const float box_y = this->m_vh * 0.28f;

	sf::RectangleShape box(sf::Vector2f(box_w, box_h));
	box.setPosition(box_x, box_y);
	box.setFillColor(kBoxFill);
	box.setOutlineThickness(4.f);
	box.setOutlineColor(kBoxBorder);
	target.draw(box);

	const float first_y = box_y + 16.f + row_h / 2.f;
	for (int i = 0; i < row_count; ++i) {
		const float y = first_y + i * row_h;

		sf::Text left = make_text(this->m_font, rows[i].left, 12, kText);
		sf::FloatRect lb = left.getLocalBounds();
		left.setOrigin(0.f, lb.top + lb.height / 2.f);
		left.setPosition(box_x + 24.f, y);
		target.draw(left);

		sf::Text right = make_text(this->m_font, rows[i].right, 12, kCursor);
		sf::FloatRect rb = right.getLocalBounds();
		right.setOrigin(rb.left + rb.width, rb.top + rb.height / 2.f);
		right.setPosition(box_x + box_w - 24.f, y);
		target.draw(right);
	}

	this->draw_text(target, "ESC / ENTER : BACK", 10, cx, box_y + box_h + 28.f, kSubtitle);
}
