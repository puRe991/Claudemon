// ---------------------------------------------------------------------------
// Codemon map editor
//
// A small Dear ImGui + SFML tool for authoring the game's tile maps. Paint
// tiles from a palette onto the grid, set the player spawn, resize the map and
// save/load it in the same CSV format the game reads (via TileMap).
// ---------------------------------------------------------------------------
#include <imgui.h>
#include <imgui-SFML.h>

#include <SFML/Graphics.hpp>

#include "TileMap.h"

#include <cstring>
#include <string>

static const int TILE_SRC   = 32; // tile size in the tilesheet, pixels
static const int TILE_COUNT = 8;  // number of tiles in the sheet

int main()
{
    sf::RenderWindow window(sf::VideoMode(1180, 780), "Codemon Map Editor");
    window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(window)) {
        return 1;
    }

    sf::Texture tileset;
    const bool have_tiles = tileset.loadFromFile("maps/map_set/overworld.png");

    const char* tile_names[TILE_COUNT] = {
        "Grass", "Tall Grass", "Tree", "Water", "Sand", "Path", "Flowers", "Fence"
    };

    // Load the starter region, or start from a small blank map.
    TileMap map;
    if (!map.load_from_file("maps/region_00.txt")) {
        map = TileMap(20, 15, 0);
    }

    int   selected   = 0;
    bool  set_start  = false;
    float origin_x   = 360.f;   // where the map is drawn (leaves room for panel)
    float origin_y   = 20.f;
    const float cell = 20.f;    // on-screen tile size
    char  path_buf[256];
    std::strncpy(path_buf, "maps/region_00.txt", sizeof(path_buf));
    path_buf[sizeof(path_buf) - 1] = '\0';
    std::string status = "Ready.";

    sf::Clock clock;
    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            ImGui::SFML::ProcessEvent(window, event);
            if (event.type == sf::Event::Closed) {
                window.close();
            }
        }

        ImGui::SFML::Update(window, clock.restart());
        ImGuiIO& io = ImGui::GetIO();

        /* ---- tools panel ---- */
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(330, 760), ImGuiCond_FirstUseEver);
        ImGui::Begin("Tools");

        ImGui::TextUnformatted("Tile palette");
        for (int i = 0; i < TILE_COUNT; ++i) {
            ImGui::PushID(i);
            bool picked = false;
            if (have_tiles) {
                sf::Sprite spr(tileset);
                spr.setTextureRect(sf::IntRect(i * TILE_SRC, 0, TILE_SRC, TILE_SRC));
                picked = ImGui::ImageButton(spr, sf::Vector2f(32.f, 32.f));
            } else {
                picked = ImGui::Button(tile_names[i], ImVec2(48.f, 48.f));
            }
            if (picked) {
                selected = i;
            }
            ImGui::PopID();
            if ((i + 1) % 4 != 0) {
                ImGui::SameLine();
            }
        }
        ImGui::Text("Selected: %s (%d)", tile_names[selected], selected);
        ImGui::Checkbox("Place player start on click", &set_start);
        ImGui::TextDisabled("Left: paint   Right: erase to grass");
        ImGui::TextDisabled("Arrow keys / drag empty space: scroll");

        ImGui::Separator();
        int w = (int)map.get_width();
        int h = (int)map.get_height();
        ImGui::PushItemWidth(90);
        ImGui::InputInt("W", &w);
        ImGui::SameLine();
        ImGui::InputInt("H", &h);
        ImGui::PopItemWidth();
        if (ImGui::Button("Resize")) {
            if (w > 0 && h > 0 && w <= 300 && h <= 300) {
                map.resize((unsigned int)w, (unsigned int)h, 0);
                status = "Resized to " + std::to_string(w) + "x" + std::to_string(h);
            }
        }

        ImGui::Separator();
        ImGui::PushItemWidth(-1);
        ImGui::InputText("##file", path_buf, sizeof(path_buf));
        ImGui::PopItemWidth();
        if (ImGui::Button("Save")) {
            status = map.save_to_file(path_buf) ? std::string("Saved ") + path_buf
                                                : std::string("Save FAILED: ") + path_buf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            status = map.load_from_file(path_buf) ? std::string("Loaded ") + path_buf
                                                  : std::string("Load FAILED: ") + path_buf;
        }

        ImGui::Separator();
        ImGui::Text("Start tile: %u, %u", map.get_start_x(), map.get_start_y());
        ImGui::Text("Map: %u x %u", map.get_width(), map.get_height());
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::End();

        /* ---- scroll the canvas ---- */
        if (!io.WantCaptureKeyboard) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left))  origin_x += 12.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right)) origin_x -= 12.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up))    origin_y += 12.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down))  origin_y -= 12.f;
        }

        /* ---- paint ---- */
        if (!io.WantCaptureMouse) {
            const sf::Vector2i mp = sf::Mouse::getPosition(window);
            const int tx = (int)((mp.x - origin_x) / cell);
            const int ty = (int)((mp.y - origin_y) / cell);
            const bool in_map = tx >= 0 && ty >= 0 && map.in_bounds((unsigned int)tx, (unsigned int)ty);
            if (in_map && sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
                if (set_start) map.set_start((unsigned int)tx, (unsigned int)ty);
                else           map.set_tile((unsigned int)tx, (unsigned int)ty, selected);
            } else if (in_map && sf::Mouse::isButtonPressed(sf::Mouse::Right)) {
                map.set_tile((unsigned int)tx, (unsigned int)ty, 0);
            }
        }

        /* ---- render ---- */
        window.clear(sf::Color(28, 28, 38));

        if (have_tiles) {
            sf::Sprite tile(tileset);
            tile.setScale(cell / TILE_SRC, cell / TILE_SRC);
            for (unsigned int y = 0; y < map.get_height(); ++y) {
                for (unsigned int x = 0; x < map.get_width(); ++x) {
                    int id = map.get_tile(x, y);
                    if (id < 0) id = 0;
                    tile.setTextureRect(sf::IntRect(id * TILE_SRC, 0, TILE_SRC, TILE_SRC));
                    tile.setPosition(origin_x + x * cell, origin_y + y * cell);
                    window.draw(tile);
                }
            }
        }

        // Grid lines.
        sf::VertexArray grid(sf::Lines);
        const sf::Color gc(0, 0, 0, 60);
        for (unsigned int x = 0; x <= map.get_width(); ++x) {
            grid.append(sf::Vertex(sf::Vector2f(origin_x + x * cell, origin_y), gc));
            grid.append(sf::Vertex(sf::Vector2f(origin_x + x * cell, origin_y + map.get_height() * cell), gc));
        }
        for (unsigned int y = 0; y <= map.get_height(); ++y) {
            grid.append(sf::Vertex(sf::Vector2f(origin_x, origin_y + y * cell), gc));
            grid.append(sf::Vertex(sf::Vector2f(origin_x + map.get_width() * cell, origin_y + y * cell), gc));
        }
        window.draw(grid);

        // Player-start marker.
        sf::RectangleShape marker(sf::Vector2f(cell, cell));
        marker.setPosition(origin_x + map.get_start_x() * cell, origin_y + map.get_start_y() * cell);
        marker.setFillColor(sf::Color(255, 0, 0, 0));
        marker.setOutlineColor(sf::Color(230, 40, 40));
        marker.setOutlineThickness(2.f);
        window.draw(marker);

        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown();
    return 0;
}
