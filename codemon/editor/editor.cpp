// ---------------------------------------------------------------------------
// codemon_editor - a standalone, external map editor for the Monsta engine.
//
// It is deliberately separate from the game: it loads the same terrain sheet
// (region/region_tiles.png) and reads/writes the same ".map" files the engine
// consumes, but runs on its own. Paint tiles with the mouse, pick from the
// palette, and save.
//
//   codemon_editor [mapfile] [sheet.png] [W H]
//       Open <mapfile> (default region/maps/pallet_town.map). If it does not
//       exist it is created as a W x H grass map (default 15 x 13).
//   codemon_editor --selftest
//       Headless: exercise map load/save round-tripping. No window. For CTest.
//   codemon_editor --shot <out.png> [mapfile] [sheet.png]
//       Render one editor frame to a PNG and exit (needs a display).
//
// Controls: left-click palette = pick tile; left-drag canvas = paint; right-
// drag canvas = erase to grass; [ / ] = cycle tile; G = toggle grid; S = save.
// Live status (file, selected tile, hovered cell, "saved") is in the title bar.
// ---------------------------------------------------------------------------
#include <SFML/Graphics.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static const int TILE = 32;   // engine tile size, in pixels

// ---- the editable grid ----------------------------------------------------
struct EditorMap {
    int w = 0, h = 0;
    std::vector<int> tiles;    // row-major, w*h

    void create(int width, int height, int fill = 0) {
        w = width; h = height;
        tiles.assign((size_t)w * h, fill);
    }
    int at(int x, int y) const { return tiles[(size_t)y * w + x]; }
    void set(int x, int y, int id) {
        if (x >= 0 && x < w && y >= 0 && y < h) tiles[(size_t)y * w + x] = id;
    }
    bool in_bounds(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }

    // Parse "W,H" header + H rows of comma-separated ids. False if unreadable.
    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) return false;
        std::string line;
        if (!std::getline(in, line)) return false;
        std::stringstream hs(line);
        std::string a, b;
        if (!std::getline(hs, a, ',') || !std::getline(hs, b, ',')) return false;
        int width = std::atoi(a.c_str()), height = std::atoi(b.c_str());
        if (width <= 0 || height <= 0) return false;
        create(width, height, 0);
        for (int y = 0; y < height; ++y) {
            if (!std::getline(in, line)) return false;
            std::stringstream rs(line);
            std::string cell;
            for (int x = 0; x < width; ++x) {
                if (!std::getline(rs, cell, ',')) return false;
                set(x, y, std::atoi(cell.c_str()));
            }
        }
        return true;
    }

    bool save(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << w << "," << h << "\n";
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                out << at(x, y);
                if (x < w - 1) out << ",";
            }
            out << "\n";
        }
        return true;
    }
};

// ---- headless round-trip test --------------------------------------------
static int run_selftest() {
    const std::string tmp = "._editor_selftest.map";
    EditorMap m;
    m.create(6, 4, 0);
    m.set(2, 1, 16);
    m.set(5, 3, 21);
    m.set(0, 0, 7);
    if (!m.save(tmp)) { std::printf("FAIL: save\n"); return 1; }

    EditorMap n;
    if (!n.load(tmp)) { std::printf("FAIL: load\n"); return 1; }
    std::remove(tmp.c_str());

    int fails = 0;
    if (n.w != 6 || n.h != 4) { std::printf("FAIL: dims %dx%d\n", n.w, n.h); ++fails; }
    if (n.at(2, 1) != 16) { std::printf("FAIL: (2,1)=%d\n", n.at(2, 1)); ++fails; }
    if (n.at(5, 3) != 21) { std::printf("FAIL: (5,3)=%d\n", n.at(5, 3)); ++fails; }
    if (n.at(0, 0) != 7) { std::printf("FAIL: (0,0)=%d\n", n.at(0, 0)); ++fails; }
    if (n.at(1, 1) != 0) { std::printf("FAIL: (1,1)=%d\n", n.at(1, 1)); ++fails; }

    if (fails == 0) { std::printf("editor selftest OK\n"); return 0; }
    std::printf("editor selftest FAILED: %d\n", fails);
    return 1;
}

// ---- editor view geometry -------------------------------------------------
struct Layout {
    int pal_cols = 5, pal_gap = 3, margin = 10;
    int pal_x, pal_y, pal_w;
    int canvas_x, canvas_y;
    int tile_count;

    Layout(int tile_count_) : tile_count(tile_count_) {
        pal_x = margin; pal_y = margin;
        pal_w = pal_cols * (TILE + pal_gap);
        canvas_x = margin + pal_w + margin + 5;
        canvas_y = margin;
    }
    int pal_rows() const { return (tile_count + pal_cols - 1) / pal_cols; }
    // palette cell rect for tile id
    sf::IntRect pal_rect(int id) const {
        int c = id % pal_cols, r = id / pal_cols;
        return sf::IntRect(pal_x + c * (TILE + pal_gap), pal_y + r * (TILE + pal_gap), TILE, TILE);
    }
    // which palette id is under (mx,my), or -1
    int pal_hit(int mx, int my) const {
        for (int id = 0; id < tile_count; ++id) {
            sf::IntRect r = pal_rect(id);
            if (r.contains(mx, my)) return id;
        }
        return -1;
    }
};

// tile sprite from the one-row sheet (x = id * TILE)
static sf::Sprite tile_sprite(const sf::Texture& sheet, int id, int px, int py) {
    sf::Sprite s(sheet);
    s.setTextureRect(sf::IntRect(id * TILE, 0, TILE, TILE));
    s.setPosition((float)px, (float)py);
    return s;
}

static void draw_scene(sf::RenderTarget& target, const EditorMap& map,
                       const sf::Texture& sheet, const Layout& lay,
                       int selected, bool show_grid, int hover_x, int hover_y) {
    target.clear(sf::Color(38, 40, 46));

    // palette
    for (int id = 0; id < lay.tile_count; ++id) {
        sf::IntRect r = lay.pal_rect(id);
        target.draw(tile_sprite(sheet, id, r.left, r.top));
    }
    // selected palette highlight
    {
        sf::IntRect r = lay.pal_rect(selected);
        sf::RectangleShape box(sf::Vector2f((float)r.width + 4, (float)r.height + 4));
        box.setPosition((float)r.left - 2, (float)r.top - 2);
        box.setFillColor(sf::Color::Transparent);
        box.setOutlineThickness(2);
        box.setOutlineColor(sf::Color(255, 220, 60));
        target.draw(box);
    }

    // canvas tiles
    for (int y = 0; y < map.h; ++y) {
        for (int x = 0; x < map.w; ++x) {
            target.draw(tile_sprite(sheet, map.at(x, y),
                                    lay.canvas_x + x * TILE, lay.canvas_y + y * TILE));
        }
    }
    // grid
    if (show_grid) {
        sf::Color gc(0, 0, 0, 70);
        for (int x = 0; x <= map.w; ++x) {
            sf::RectangleShape v(sf::Vector2f(1.f, (float)map.h * TILE));
            v.setPosition((float)(lay.canvas_x + x * TILE), (float)lay.canvas_y);
            v.setFillColor(gc);
            target.draw(v);
        }
        for (int y = 0; y <= map.h; ++y) {
            sf::RectangleShape hln(sf::Vector2f((float)map.w * TILE, 1.f));
            hln.setPosition((float)lay.canvas_x, (float)(lay.canvas_y + y * TILE));
            hln.setFillColor(gc);
            target.draw(hln);
        }
    }
    // hovered cell
    if (map.in_bounds(hover_x, hover_y)) {
        sf::RectangleShape hl(sf::Vector2f((float)TILE, (float)TILE));
        hl.setPosition((float)(lay.canvas_x + hover_x * TILE),
                       (float)(lay.canvas_y + hover_y * TILE));
        hl.setFillColor(sf::Color(255, 255, 255, 60));
        hl.setOutlineThickness(2);
        hl.setOutlineColor(sf::Color(255, 255, 255, 180));
        target.draw(hl);
    }
}

// ---- one-frame screenshot mode -------------------------------------------
static int run_shot(const std::string& out, const std::string& mapfile,
                    const std::string& sheetfile) {
    sf::Texture sheet;
    if (!sheet.loadFromFile(sheetfile)) { std::printf("no sheet %s\n", sheetfile.c_str()); return 1; }
    int tile_count = (int)sheet.getSize().x / TILE;
    EditorMap map;
    if (!map.load(mapfile)) map.create(15, 13, 0);
    Layout lay(tile_count);
    int win_w = lay.canvas_x + map.w * TILE + lay.margin;
    int win_h = std::max(lay.pal_y + lay.pal_rows() * (TILE + lay.pal_gap),
                         lay.canvas_y + map.h * TILE) + lay.margin;
    sf::RenderTexture rt;
    if (!rt.create(win_w, win_h)) { std::printf("rt fail\n"); return 1; }
    draw_scene(rt, map, sheet, lay, 21, true, 7, 6);
    rt.display();
    if (!rt.getTexture().copyToImage().saveToFile(out)) { std::printf("save fail\n"); return 1; }
    std::printf("wrote %s (%dx%d)\n", out.c_str(), win_w, win_h);
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--selftest") {
        return run_selftest();
    }
    if (argc >= 3 && std::string(argv[1]) == "--shot") {
        std::string out = argv[2];
        std::string mapfile = (argc >= 4) ? argv[3] : "region/maps/pallet_town.map";
        std::string sheetfile = (argc >= 5) ? argv[4] : "region/region_tiles.png";
        return run_shot(out, mapfile, sheetfile);
    }

    std::string mapfile = (argc >= 2) ? argv[1] : "region/maps/pallet_town.map";
    std::string sheetfile = (argc >= 3) ? argv[2] : "region/region_tiles.png";

    sf::Texture sheet;
    if (!sheet.loadFromFile(sheetfile)) {
        std::printf("editor: cannot load tile sheet '%s'\n", sheetfile.c_str());
        return 1;
    }
    int tile_count = (int)sheet.getSize().x / TILE;

    EditorMap map;
    if (!map.load(mapfile)) {
        int nw = (argc >= 5) ? std::atoi(argv[3]) : 15;
        int nh = (argc >= 5) ? std::atoi(argv[4]) : 13;
        if (nw <= 0) nw = 15;
        if (nh <= 0) nh = 13;
        map.create(nw, nh, 0);
        std::printf("editor: '%s' not found, created a %dx%d grass map\n",
                    mapfile.c_str(), nw, nh);
    }

    Layout lay(tile_count);
    int win_w = lay.canvas_x + map.w * TILE + lay.margin;
    int win_h = std::max(lay.pal_y + lay.pal_rows() * (TILE + lay.pal_gap),
                         lay.canvas_y + map.h * TILE) + lay.margin;

    sf::RenderWindow win(sf::VideoMode(win_w, win_h), "codemon editor");
    win.setFramerateLimit(60);

    int selected = 0;
    bool show_grid = true, painting = false, erasing = false, saved_flash = false;
    int hover_x = -1, hover_y = -1;

    auto cell_at = [&](int mx, int my, int& cx, int& cy) {
        cx = (mx - lay.canvas_x) / TILE;
        cy = (my - lay.canvas_y) / TILE;
        // guard against negative truncation toward zero
        if (mx < lay.canvas_x || my < lay.canvas_y) { cx = -1; cy = -1; }
    };

    while (win.isOpen()) {
        sf::Event e;
        while (win.pollEvent(e)) {
            if (e.type == sf::Event::Closed) win.close();
            else if (e.type == sf::Event::MouseButtonPressed) {
                int mx = e.mouseButton.x, my = e.mouseButton.y;
                if (e.mouseButton.button == sf::Mouse::Left) {
                    int p = lay.pal_hit(mx, my);
                    if (p >= 0) { selected = p; }
                    else {
                        int cx, cy; cell_at(mx, my, cx, cy);
                        if (map.in_bounds(cx, cy)) { map.set(cx, cy, selected); painting = true; }
                    }
                } else if (e.mouseButton.button == sf::Mouse::Right) {
                    erasing = true;
                    int cx, cy; cell_at(mx, my, cx, cy);
                    if (map.in_bounds(cx, cy)) map.set(cx, cy, 0);
                }
            }
            else if (e.type == sf::Event::MouseButtonReleased) {
                if (e.mouseButton.button == sf::Mouse::Left) painting = false;
                if (e.mouseButton.button == sf::Mouse::Right) erasing = false;
            }
            else if (e.type == sf::Event::MouseMoved) {
                int mx = e.mouseMove.x, my = e.mouseMove.y;
                cell_at(mx, my, hover_x, hover_y);
                if (painting && map.in_bounds(hover_x, hover_y)) map.set(hover_x, hover_y, selected);
                if (erasing && map.in_bounds(hover_x, hover_y)) map.set(hover_x, hover_y, 0);
            }
            else if (e.type == sf::Event::KeyPressed) {
                if (e.key.code == sf::Keyboard::S) {
                    saved_flash = map.save(mapfile);
                } else if (e.key.code == sf::Keyboard::G) {
                    show_grid = !show_grid;
                } else if (e.key.code == sf::Keyboard::RBracket) {
                    selected = (selected + 1) % tile_count;
                } else if (e.key.code == sf::Keyboard::LBracket) {
                    selected = (selected + tile_count - 1) % tile_count;
                }
            }
        }

        // live status in the title bar (no font dependency)
        std::stringstream title;
        title << "codemon editor  |  " << mapfile << "  |  tile " << selected
              << "  |  cell ";
        if (map.in_bounds(hover_x, hover_y)) title << hover_x << "," << hover_y;
        else title << "-";
        title << "  |  [S]ave [G]rid [ ] tile";
        if (saved_flash) title << "   *saved*";
        win.setTitle(title.str());
        saved_flash = false;

        draw_scene(win, map, sheet, lay, selected, show_grid, hover_x, hover_y);
        win.display();
    }
    return 0;
}
