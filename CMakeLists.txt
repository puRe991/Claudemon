# ---------------------------------------------------------------------------
# Cod-e-mon / Monsta engine build script
#
# Portable CMake build for the SFML 2.x based tile game. Produces the game
# executable `codemon` and a headless unit-test binary `codemon_tests` that is
# wired into CTest so the core logic can be verified without a display.
#
#   cmake -S . -B build
#   cmake --build build
#   ctest --test-dir build --output-on-failure   # run the tests
#   ./build/codemon                              # start the game (needs a display)
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)

project(codemon
    VERSION 0.1.0
    DESCRIPTION "2D tile based Pokemon-style game on the Monsta engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- SFML -------------------------------------------------------------------
# Uses a system/toolchain provided SFML 2.x. On Debian/Ubuntu:
#   sudo apt-get install libsfml-dev
# The Windows .lib files bundled under codemon/SFML are MSVC specific and are
# intentionally NOT used here so the same tree cross builds on Linux/macOS.
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/codemon")

# Core game/logic translation units shared by the game and the tests.
set(ENGINE_SOURCES
    "${SRC_DIR}/Coordinates.cpp"
    "${SRC_DIR}/Tile.cpp"
    "${SRC_DIR}/Window.cpp"
    "${SRC_DIR}/character.cpp"
    "${SRC_DIR}/map.cpp"
    "${SRC_DIR}/data_structures.cpp")

add_library(monsta_engine STATIC ${ENGINE_SOURCES})
target_include_directories(monsta_engine PUBLIC "${SRC_DIR}")
target_link_libraries(monsta_engine PUBLIC sfml-graphics sfml-window sfml-system)

# --- Game executable --------------------------------------------------------
add_executable(codemon "${SRC_DIR}/main.cpp")
target_link_libraries(codemon PRIVATE monsta_engine)

# The game loads its assets through paths that are relative to the working
# directory (e.g. "maps/map_00.txt", "assets/Red_player.png"). Copy them next
# to the built binary so `./build/codemon` runs from the build tree.
add_custom_command(TARGET codemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/maps"   "$<TARGET_FILE_DIR:codemon>/maps"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/assets" "$<TARGET_FILE_DIR:codemon>/assets"
    COMMENT "Staging maps/ and assets/ next to the game binary")

# --- Tests ------------------------------------------------------------------
enable_testing()

add_executable(codemon_tests "${SRC_DIR}/tests/tests.cpp")
target_link_libraries(codemon_tests PRIVATE monsta_engine)

add_test(NAME core_logic COMMAND codemon_tests)
