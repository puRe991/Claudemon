#include "DialogBox.h"
#include <sstream>

DialogBox::DialogBox() : font_ok(false), active(false), page(0) {}

bool DialogBox::load_font(const std::string& path) {
	this->font_ok = this->font.loadFromFile(path);
	return this->font_ok;
}

void DialogBox::open(const std::string& who, const std::string& line) {
	this->speaker = who;
	this->pages.clear();
	// split `line` on U+001F into pages
	const char sep = '\x1f';
	size_t start = 0;
	while (true) {
		size_t pos = line.find(sep, start);
		this->pages.push_back(line.substr(start, pos - start));
		if (pos == std::string::npos) break;
		start = pos + 1;
	}
	if (this->pages.empty()) this->pages.push_back("");
	this->page = 0;
	this->active = true;
}

void DialogBox::advance() {
	if (!this->active) return;
	if (this->page + 1 < this->pages.size()) this->page++;
	else this->active = false;
}

void DialogBox::close() { this->active = false; }
bool DialogBox::is_active() const { return this->active; }

// Greedy word wrap: append words until the measured width exceeds px_width.
std::string DialogBox::wrap(const std::string& s, unsigned int px_width,
                            unsigned int char_size) const {
	std::stringstream ss(s);
	std::string word, line, out;
	sf::Text probe;
	probe.setFont(this->font);
	probe.setCharacterSize(char_size);
	while (ss >> word) {
		std::string trial = line.empty() ? word : line + " " + word;
		probe.setString(sf::String::fromUtf8(trial.begin(), trial.end()));
		if (probe.getLocalBounds().width > (float)px_width && !line.empty()) {
			out += line + "\n";
			line = word;
		} else {
			line = trial;
		}
	}
	out += line;
	return out;
}

void DialogBox::draw(sf::RenderTarget& target) {
	if (!this->active || !this->font_ok) return;

	// Draw in pixel space regardless of the world camera.
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());

	sf::Vector2f size = target.getView().getSize();
	const float margin = 14.f;
	const float box_h = 150.f;
	const unsigned cs = 22;

	sf::RectangleShape box(sf::Vector2f(size.x - 2 * margin, box_h));
	box.setPosition(margin, size.y - box_h - margin);
	box.setFillColor(sf::Color(20, 28, 48, 235));
	box.setOutlineColor(sf::Color(245, 245, 245));
	box.setOutlineThickness(3.f);
	target.draw(box);

	float tx = box.getPosition().x + 16.f;
	float ty = box.getPosition().y + 12.f;

	if (!this->speaker.empty()) {
		sf::Text name(sf::String::fromUtf8(this->speaker.begin(), this->speaker.end()),
		              this->font, 18);
		name.setFillColor(sf::Color(150, 210, 255));
		name.setStyle(sf::Text::Bold);
		name.setPosition(tx, ty);
		target.draw(name);
		ty += 28.f;
	}

	const std::string& cur = this->pages[this->page];
	std::string wrapped = this->wrap(cur, (unsigned)(size.x - 2 * margin - 32), cs);
	sf::Text body(sf::String::fromUtf8(wrapped.begin(), wrapped.end()), this->font, cs);
	body.setFillColor(sf::Color::White);
	body.setPosition(tx, ty);
	target.draw(body);

	// bottom-right marker: more pages to come, or end of dialog
	sf::Text hint(this->page + 1 < this->pages.size() ? "v" : "x", this->font, 20);
	hint.setFillColor(sf::Color(200, 200, 200));
	hint.setPosition(box.getPosition().x + box.getSize().x - 28.f,
	                 box.getPosition().y + box_h - 30.f);
	target.draw(hint);

	target.setView(saved);
}
