#include "QuestLog.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return std::string();
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string tok;
	while (std::getline(ss, tok, sep)) out.push_back(trim(tok));
	return out;
}

// How many of the eight badge flags are set -- the one derived number worth
// having, since "come back once you have four badges" gates several places in
// the real story.
int badge_count(const GameState& gs) {
	int n = 0;
	for (int i = 1; i <= 8; ++i) {
		char buf[32];
		std::snprintf(buf, sizeof(buf), "FLAG_BADGE0%d_GET", i);
		if (gs.flag(buf)) ++n;
	}
	return n;
}

// The numeric value of a bare name: an item resolves to how many are in the
// bag, `money`/`badges` to themselves, anything else (FLAG_*/VAR_*) to its
// entry in the variable table -- which is 0 for a name nobody ever set, so an
// unknown flag reads as "not set" instead of failing the whole condition.
int value_of(const std::string& name, const GameState& gs) {
	if (name == "money") return gs.money;
	if (name == "badges") return badge_count(gs);
	if (name.rfind("ITEM_", 0) == 0) return gs.item_count(name);
	return gs.get_var(name);
}

// One term: "FLAG_X", "!FLAG_X", "VAR_X >= 3", "caught:TORCHIC".
bool eval_term(const std::string& raw, const GameState& gs) {
	std::string t = trim(raw);
	if (t.empty()) return true;
	if (t[0] == '!') return !eval_term(t.substr(1), gs);
	if (t.rfind("caught:", 0) == 0) return gs.is_caught(trim(t.substr(7)));
	if (t.rfind("seen:", 0) == 0) return gs.is_seen(trim(t.substr(5)));

	// Comparison, if there is one. Two-character operators are checked first
	// so ">=" is never read as a bare ">".
	static const char* OPS[] = {">=", "<=", "==", "!=", ">", "<", "="};
	for (const char* op : OPS) {
		size_t pos = t.find(op);
		if (pos == std::string::npos) continue;
		std::string lhs = trim(t.substr(0, pos));
		std::string rhs = trim(t.substr(pos + std::strlen(op)));
		if (lhs.empty() || rhs.empty()) continue;
		int a = value_of(lhs, gs);
		// The right side may be a number or another name (VAR_A == VAR_B).
		bool numeric = !rhs.empty() && (std::isdigit((unsigned char)rhs[0]) || rhs[0] == '-');
		int b = numeric ? std::atoi(rhs.c_str()) : value_of(rhs, gs);
		std::string o(op);
		if (o == ">=") return a >= b;
		if (o == "<=") return a <= b;
		if (o == "==" || o == "=") return a == b;
		if (o == "!=") return a != b;
		if (o == ">") return a > b;
		return a < b;
	}
	return value_of(t, gs) != 0;
}

} // namespace

bool QuestLog::eval_condition(const std::string& expr, const GameState& gs) {
	std::string e = trim(expr);
	if (e.empty()) return true;   // no condition = already satisfied
	// `&` binds tighter than `|`: split into OR groups first, then each group
	// into its AND terms.
	for (const std::string& group : split(e, '|')) {
		if (group.empty()) continue;
		bool all_true = true;
		for (const std::string& term : split(group, '&'))
			if (!term.empty() && !eval_term(term, gs)) { all_true = false; break; }
		if (all_true) return true;
	}
	return false;
}

bool QuestLog::load(const std::string& path) {
	std::ifstream f(path);
	if (!f.is_open()) return false;
	std::vector<Quest> loaded;
	std::string line;
	while (std::getline(f, line)) {
		std::string s = trim(line);
		if (s.empty() || s[0] == '#') continue;
		size_t sp = s.find(' ');
		std::string key = s.substr(0, sp);
		std::string rest = (sp == std::string::npos) ? std::string() : trim(s.substr(sp + 1));

		if (key == "quest") {
			Quest q;
			// "quest <id> [main|side]" -- the kind is optional and defaults to
			// a side quest, so a typo can never promote something into the
			// single HAUPTMISSION slot on the HUD.
			std::stringstream ss(rest);
			std::string id, kind;
			ss >> id >> kind;
			q.id = id;
			q.kind = (kind == "main") ? QuestKind::MAIN : QuestKind::SIDE;
			loaded.push_back(q);
			continue;
		}
		if (loaded.empty()) continue;   // a stray line before the first `quest`
		Quest& q = loaded.back();
		if (key == "title") q.title = rest;
		else if (key == "desc") q.description = q.description.empty() ? rest : q.description + " " + rest;
		else if (key == "start") q.start_cond = rest;
		else if (key == "done") q.done_cond = rest;
		else if (key == "step") {
			auto fields = split(rest, '|');
			QuestStep st;
			if (!fields.empty()) st.text = fields[0];
			if (fields.size() > 1) st.done_cond = fields[1];
			if (fields.size() > 2 && !fields[2].empty()) {
				std::stringstream ts(fields[2]);
				ts >> st.target_map;
				int tx, ty;
				if (ts >> tx >> ty) { st.target_x = tx; st.target_y = ty; }
			}
			q.steps.push_back(st);
		}
	}
	this->all = std::move(loaded);
	return !this->all.empty();
}

void QuestLog::refresh(const GameState& gs) {
	for (Quest& q : this->all) {
		int done_steps = 0;
		for (QuestStep& st : q.steps) {
			st.done = eval_condition(st.done_cond, gs);
			if (st.done) ++done_steps;
		}
		// A step the player has already passed stays done even if a later one
		// isn't: the current step is the first *unfinished* one, so a quest
		// whose middle condition is a transient VAR still reads sensibly.
		q.current_step = (int)q.steps.size();
		for (size_t i = 0; i < q.steps.size(); ++i)
			if (!q.steps[i].done) { q.current_step = (int)i; break; }

		bool finished = q.done_cond.empty()
		                    ? (!q.steps.empty() && done_steps == (int)q.steps.size())
		                    : eval_condition(q.done_cond, gs);
		bool started = eval_condition(q.start_cond, gs);
		q.status = finished ? QuestStatus::DONE
		                    : (started ? QuestStatus::ACTIVE : QuestStatus::HIDDEN);
		q.percent = q.steps.empty()
		                ? (finished ? 100 : 0)
		                : (finished ? 100 : done_steps * 100 / (int)q.steps.size());
	}
}

std::vector<int> QuestLog::active(QuestKind kind) const {
	std::vector<int> out;
	for (size_t i = 0; i < this->all.size(); ++i)
		if (this->all[i].status == QuestStatus::ACTIVE && this->all[i].kind == kind)
			out.push_back((int)i);
	return out;
}

std::vector<int> QuestLog::completed() const {
	std::vector<int> out;
	for (size_t i = 0; i < this->all.size(); ++i)
		if (this->all[i].status == QuestStatus::DONE) out.push_back((int)i);
	return out;
}

int QuestLog::find(const std::string& id) const {
	if (id.empty()) return -1;
	for (size_t i = 0; i < this->all.size(); ++i)
		if (this->all[i].id == id) return (int)i;
	return -1;
}

int QuestLog::tracked(const GameState& gs) const {
	int pick = find(gs.tracked_quest);
	if (pick >= 0 && this->all[pick].status == QuestStatus::ACTIVE) return pick;
	std::vector<int> mains = active(QuestKind::MAIN);
	if (!mains.empty()) return mains.front();
	std::vector<int> sides = active(QuestKind::SIDE);
	return sides.empty() ? -1 : sides.front();
}

const Quest* QuestLog::tracked_quest(const GameState& gs) const {
	int i = tracked(gs);
	return i < 0 ? nullptr : &this->all[i];
}

const QuestStep* QuestLog::tracked_step(const GameState& gs) const {
	const Quest* q = tracked_quest(gs);
	if (!q || q->current_step >= (int)q->steps.size()) return nullptr;
	return &q->steps[q->current_step];
}
