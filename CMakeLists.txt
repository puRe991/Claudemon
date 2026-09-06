# ---------------------------------------------------------------------------
# Cod-e-mon / Monsta engine build script
#
# Portable CMake build for the SFML 2.x based tile game. Produces the game
# executable `codemon` plus two CTest binaries: `codemon_tests` (small value
# types, no display needed) and `codemon_engine_tests` (BattleData/SaveGame
# always, plus ScriptVM/Battle when a display is available).
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
# Linux/macOS use a system/toolchain provided SFML 2.x. On Debian/Ubuntu:
#   sudo apt-get install libsfml-dev
#
# On Windows, if the caller did not point CMake at an SFML install, fall back
# to the MSVC SFML that ships with the repo under codemon/SFML. That bundled
# build has no runtime DLLs (only static + import libraries), so link SFML
# statically there to produce a self-contained codemon.exe. That bundled copy
# is 64-bit only, though -- for a 32-bit (Win32) build, there's nothing to
# point SFML_DIR at, so CODEMON_FETCH_SFML instead pulls SFML's source via
# FetchContent and builds it as part of this project (SFML's own CMake uses
# its bundled extlibs on Windows, so this needs no extra dependencies). The
# Windows launcher also turns this on automatically as a fallback if linking
# the bundled MSVC libraries fails.
option(CODEMON_FETCH_SFML "Build SFML from source via FetchContent instead of using a bundled/system install (needed for 32-bit Windows, which has no bundled SFML)" OFF)

if(CODEMON_FETCH_SFML)
    message(STATUS "SFML: building from source via FetchContent")
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SFML_USE_STATIC_STD_LIBS ON CACHE BOOL "" FORCE)
    # SFML 2.5.1's own source still uses std::auto_ptr, which C++17 removed
    # from the standard library outright. CMAKE_CXX_STANDARD is read when a
    # target is created and would otherwise leak into SFML's own targets
    # (add_subdirectory'd below), so unset it around the fetch and restore it
    # for this project's own targets afterwards.
    set(CODEMON_SAVED_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
    unset(CMAKE_CXX_STANDARD)
    FetchContent_Declare(
        SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        2.5.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SFML)
    set(CMAKE_CXX_STANDARD "${CODEMON_SAVED_CXX_STANDARD}")
elseif(WIN32 AND NOT DEFINED SFML_DIR AND NOT DEFINED ENV{SFML_DIR})
    set(SFML_STATIC_LIBRARIES TRUE)
    set(SFML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/codemon/SFML/lib/cmake/SFML")
endif()

if(NOT CODEMON_FETCH_SFML)
    # audio is required unconditionally: the engine's Audio class (cries,
    # music, SFX) links sfml-audio regardless of how SFML was obtained.
    set(SFML_COMPONENTS graphics window system audio)
    if(WIN32)
        # sfml-main provides the WinMain -> main shim used by the VS project.
        list(APPEND SFML_COMPONENTS main)
    endif()
    find_package(SFML 2.5 COMPONENTS ${SFML_COMPONENTS} REQUIRED)
endif()

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/codemon")

# Core game/logic translation units shared by the game and the tests.
set(ENGINE_SOURCES
    "${SRC_DIR}/Coordinates.cpp"
    "${SRC_DIR}/Tile.cpp"
    "${SRC_DIR}/TileMap.cpp"
    "${SRC_DIR}/Tileset.cpp"
    "${SRC_DIR}/Window.cpp"
    "${SRC_DIR}/character.cpp"
    "${SRC_DIR}/map.cpp"
    "${SRC_DIR}/Region.cpp"
    "${SRC_DIR}/Audio.cpp"
    "${SRC_DIR}/DialogBox.cpp"
    "${SRC_DIR}/BattleData.cpp"
    "${SRC_DIR}/PartySystem.cpp"
    "${SRC_DIR}/Battle.cpp"
    "${SRC_DIR}/ScriptVM.cpp"
    "${SRC_DIR}/Menu.cpp"
    "${SRC_DIR}/Minigame.cpp"
    "${SRC_DIR}/SaveGame.cpp"
    "${SRC_DIR}/QuestLog.cpp"
    "${SRC_DIR}/Bike.cpp"
    "${SRC_DIR}/view_utils.cpp"
    "${SRC_DIR}/MenuModel.cpp"
    "${SRC_DIR}/MainMenu.cpp")

add_library(monsta_engine STATIC ${ENGINE_SOURCES})
target_include_directories(monsta_engine PUBLIC "${SRC_DIR}")
target_link_libraries(monsta_engine PUBLIC sfml-graphics sfml-window sfml-system sfml-audio)

# --- Game executable --------------------------------------------------------
add_executable(codemon "${SRC_DIR}/main.cpp")
target_link_libraries(codemon PRIVATE monsta_engine)
if(WIN32)
    target_link_libraries(codemon PRIVATE sfml-main)
endif()

# The game loads its assets through paths that are relative to the working
# directory (e.g. "maps/map_00.txt", "assets/Red_player.png"). Copy them next
# to the built binary so `./build/codemon` runs from the build tree.
#
# This is its own always-run target rather than a POST_BUILD on codemon: as a
# POST_BUILD it only fires when the executable itself relinks, so re-running
# the importer and rebuilding left the staged copy stale -- and the engine
# tests read their maps from there, so they would test yesterday's data.
#
# The destination uses $<TARGET_FILE_DIR:codemon> rather than
# CMAKE_CURRENT_BINARY_DIR: they're the same directory for a single-config
# generator (Makefiles/Ninja), but a multi-config generator (Visual Studio,
# used by the Windows CI build) actually puts codemon.exe in a per-config
# subdirectory like build/Release -- CMAKE_CURRENT_BINARY_DIR is still just
# build/, which would stage maps/assets next to nothing. That mismatch is
# exactly what made the Windows build's own ctest run fail with "could not
# load assets/battle" (and would have shipped a codemon.exe that can't find
# its assets either).
add_custom_target(stage_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/maps"   "$<TARGET_FILE_DIR:codemon>/maps"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/assets" "$<TARGET_FILE_DIR:codemon>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/region" "$<TARGET_FILE_DIR:codemon>/region"
    COMMENT "Staging maps/, assets/ and region/ next to the game binary")
# stage_assets depends on codemon (not the other way around): it only needs
# to know where codemon's binary ended up (to stage next to it), not to run
# before codemon links -- declaring it the other way around would make
# codemon depend on stage_assets while stage_assets's own command depends on
# codemon's target file, an actual dependency cycle CMake refuses to
# generate.
add_dependencies(stage_assets codemon)

if(WIN32)
    # SFML's audio module always dynamically loads OpenAL Soft's DLL at
    # runtime, even when SFML itself is statically linked (neither the
    # bundled codemon/SFML nor a from-source CODEMON_FETCH_SFML build copy it
    # anywhere), so without this codemon.exe fails to start with "openAL32.dll
    # not found". redist/ carries the matching official DLL for each
    # architecture (from the same SFML 2.5.1 release codemon/SFML is built
    # from).
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CODEMON_OPENAL_DLL "${SRC_DIR}/redist/openal32-x64.dll")
    else()
        set(CODEMON_OPENAL_DLL "${SRC_DIR}/redist/openal32-x86.dll")
    endif()
    add_custom_command(TARGET codemon POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CODEMON_OPENAL_DLL}" "$<TARGET_FILE_DIR:codemon>/openal32.dll"
        COMMENT "Staging openal32.dll next to the game binary")
endif()

# --- External map editor ----------------------------------------------------
# A standalone tool (separate from the game) for painting the .map files.
add_executable(codemon_editor "${SRC_DIR}/editor/editor.cpp")
target_link_libraries(codemon_editor PRIVATE monsta_engine)
if(WIN32)
    target_link_libraries(codemon_editor PRIVATE sfml-main)
endif()

# The editor reads/writes region/ (tile sheet + maps) relative to the working
# directory; stage it next to the editor binary too.
add_custom_command(TARGET codemon_editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/region" "$<TARGET_FILE_DIR:codemon_editor>/region"
    COMMENT "Staging region/ next to the editor binary")

# --- Map editor (Dear ImGui) ------------------------------------------------
# Optional so lean/cross builds (e.g. the Win32 game package) can skip pulling
# ImGui. Fetches Dear ImGui + ImGui-SFML and reuses the SFML resolved above.
option(CODEMON_BUILD_EDITOR "Build the ImGui-based map editor" ON)

if(CODEMON_BUILD_EDITOR)
    include(FetchContent)

    # Dear ImGui ships no CMakeLists; ImGui-SFML compiles it from IMGUI_DIR.
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.9)
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()
    set(IMGUI_DIR "${imgui_SOURCE_DIR}")

    set(IMGUI_SFML_FIND_SFML OFF)   # reuse the SFML targets resolved above
    set(IMGUI_SFML_IMGUI_DEMO OFF)
    FetchContent_Declare(imgui-sfml
        GIT_REPOSITORY https://github.com/SFML/imgui-sfml.git
        GIT_TAG        v2.6)
    FetchContent_MakeAvailable(imgui-sfml)

    add_executable(codemon_editor_imgui "${CMAKE_CURRENT_SOURCE_DIR}/editor/editor_main.cpp")
    target_link_libraries(codemon_editor_imgui PRIVATE monsta_engine ImGui-SFML::ImGui-SFML)

    add_custom_command(TARGET codemon_editor_imgui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SRC_DIR}/maps"   "$<TARGET_FILE_DIR:codemon_editor_imgui>/maps"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SRC_DIR}/assets" "$<TARGET_FILE_DIR:codemon_editor_imgui>/assets"
        COMMENT "Staging maps/ and assets/ next to the editor binary")
endif()

# --- Tests ------------------------------------------------------------------
enable_testing()

add_executable(codemon_tests "${SRC_DIR}/tests/tests.cpp")
target_link_libraries(codemon_tests PRIVATE monsta_engine)

# Point the tests at the in-tree region manifest so they can validate the
# region structure no matter where CTest launches them from.
target_compile_definitions(codemon_tests PRIVATE
    REGION_MANIFEST_PATH="${SRC_DIR}/region/kanto.region")

add_test(NAME core_logic COMMAND codemon_tests)

# Regression tests for the real game logic (BattleData/SaveGame/ScriptVM/
# Battle). These read the imported battle tables and a fixture map, so they run
# from the staged asset tree next to the game binary; the graphics-dependent
# half skips itself when there is no display (see has_display() in the source).
add_executable(codemon_engine_tests "${SRC_DIR}/tests/engine_tests.cpp")
target_link_libraries(codemon_engine_tests PRIVATE monsta_engine)
add_dependencies(codemon_engine_tests stage_assets)   # reads the staged maps/ + assets/
add_custom_command(TARGET codemon_engine_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SRC_DIR}/tests/fixtures"
            "$<TARGET_FILE_DIR:codemon_engine_tests>/tests/fixtures"
    COMMENT "Staging test fixtures next to the test binary")

add_test(NAME engine_logic COMMAND codemon_engine_tests)
set_tests_properties(engine_logic PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:codemon_engine_tests>")

# Headless round-trip test of the editor's map load/save (no display needed).
add_test(NAME editor_selftest COMMAND codemon_editor --selftest)
