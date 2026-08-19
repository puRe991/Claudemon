#include "Region.h"

#include <fstream>
#include <sstream>

// Split a line on '|' into its fields.
static std::vector<std::string> split_pipe(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '|')) {
        fields.push_back(field);
    }
    return fields;
}

Region::Region(const std::string& manifest_path) {
    this->is_loaded = false;

    std::ifstream in(manifest_path);
    if (!in.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        // Trim a trailing carriage return so CRLF manifests parse cleanly.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Skip blank lines and comments.
        if (line.empty() || line[0] == '#') {
            continue;
        }
        this->parse_line(line);
    }
    in.close();

    // A region needs at least one area to be usable.
    this->is_loaded = !this->area_list.empty();
}

void Region::parse_line(const std::string& line) {
    std::vector<std::string> f = split_pipe(line);
    if (f.empty()) {
        return;
    }

    if (f[0] == "START" && f.size() >= 2) {
        this->start_id = f[1];
    }
    else if (f[0] == "AREA" && f.size() >= 6) {
        RegionArea area;
        area.id = f[1];
        area.name = f[2];
        area.map_path = f[3];
        area.start = Coordinates((unsigned int)std::stoi(f[4]),
                                 (unsigned int)std::stoi(f[5]));
        this->area_list.push_back(area);
    }
    else if (f[0] == "WARP" && f.size() >= 7) {
        RegionWarp warp;
        warp.from_id = f[1];
        warp.from_pos = Coordinates((unsigned int)std::stoi(f[2]),
                                    (unsigned int)std::stoi(f[3]));
        warp.to_id = f[4];
        warp.to_pos = Coordinates((unsigned int)std::stoi(f[5]),
                                  (unsigned int)std::stoi(f[6]));
        this->warp_list.push_back(warp);
    }
    // Unknown record types are ignored so the format can grow later.
}

bool Region::loaded() const {
    return this->is_loaded;
}

const RegionArea* Region::start_area() const {
    if (!this->start_id.empty()) {
        const RegionArea* a = this->get_area(this->start_id);
        if (a != nullptr) {
            return a;
        }
    }
    // Fall back to the first declared area if START is missing.
    if (!this->area_list.empty()) {
        return &this->area_list.front();
    }
    return nullptr;
}

const RegionArea* Region::get_area(const std::string& id) const {
    for (const RegionArea& a : this->area_list) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

const RegionWarp* Region::warp_at(const std::string& area_id,
                                  Coordinates pos) const {
    for (const RegionWarp& w : this->warp_list) {
        if (w.from_id == area_id &&
            w.from_pos.get_x() == pos.get_x() &&
            w.from_pos.get_y() == pos.get_y()) {
            return &w;
        }
    }
    return nullptr;
}

std::vector<std::string> Region::neighbors(const std::string& area_id) const {
    std::vector<std::string> result;
    for (const RegionWarp& w : this->warp_list) {
        if (w.from_id != area_id) {
            continue;
        }
        bool seen = false;
        for (const std::string& n : result) {
            if (n == w.to_id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            result.push_back(w.to_id);
        }
    }
    return result;
}

std::size_t Region::area_count() const {
    return this->area_list.size();
}

std::size_t Region::warp_count() const {
    return this->warp_list.size();
}

const std::vector<RegionArea>& Region::areas() const {
    return this->area_list;
}

const std::vector<RegionWarp>& Region::warps() const {
    return this->warp_list;
}
