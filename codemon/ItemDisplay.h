#pragma once
#include <cctype>
#include <string>

// "ITEM_POKE_BALL" -> "Poke Ball"
static std::string item_display_name(const std::string& item) {
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
