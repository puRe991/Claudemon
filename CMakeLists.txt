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
# Linux/macOS use a system/toolchain provided SFML 2.x. On Debian/Ubuntu:
#   sudo apt-get install libsfml-dev
#
# On Windows, if the caller did not point CMake at an SFML install, fall back
# to the MSVC SFML that ships with the repo under codemon/SFML. That bundled
# build has no runtime DLLs (only static + import libraries), so link SFML
# statically there to produce a self-contained codemon.exe.
# CODEMON_FETCH_SFML builds SFML from source (guaranteeing an ABI match with
# the local compiler) instead of using a system/bundled SFML. The Windows
# launcher turns this on automatically as a fallback if linking the bundled
# MSVC libraries fails. Default OFF keeps the Linux/macOS path using system SFML.
option(CODEMON_FETCH_SFML "Build SFML from source via FetchContent" OFF)

if(CODEMON_FETCH_SFML)
    message(STATUS "SFML: building from source via FetchContent")
    include(FetchContent)
    set(SFML_BUILD_AUDIO   OFF CACHE BOOL "" FORCE)
    set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE) # static -> self-contained binary
    FetchContent_Declare(SFML
        GIT_REPOSITORY https://github.com/SFML/SFML.git
        GIT_TAG        2.6.2)
    FetchContent_MakeAvailable(SFML)
else()
    if(WIN32 AND NOT DEFINED SFML_DIR AND NOT DEFINED ENV{SFML_DIR})
        set(SFML_STATIC_LIBRARIES TRUE)
        set(SFML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/codemon/SFML/lib/cmake/SFML")
    endif()
    set(SFML_COMPONENTS graphics window system)
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
if(WIN32)
    target_link_libraries(codemon PRIVATE sfml-main)
endif()

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
