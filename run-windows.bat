@echo off
REM ===========================================================================
REM  Cod-e-mon - Windows launcher
REM
REM  Configures and builds the project with CMake, then starts the game.
REM  By default it uses the MSVC SFML bundled under codemon\SFML (linked
REM  statically, so no SFML DLLs are needed). To use your own SFML instead,
REM  set SFML_DIR before running, e.g.:
REM      set SFML_DIR=C:\SFML\lib\cmake\SFML
REM
REM  Requirements: CMake and a C++ toolchain (Visual Studio Build Tools).
REM  Usage:  run-windows.bat            (Release build, then run)
REM          run-windows.bat Debug      (Debug build, then run)
REM ===========================================================================
setlocal enableextensions

REM Always operate from the directory this script lives in.
cd /d "%~dp0"

REM Build configuration (Release by default).
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake was not found on PATH.
    echo         Install it from https://cmake.org/download/ and re-run.
    exit /b 1
)

echo === Configuring (CMake) ===
cmake -S . -B build
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

echo === Building (%CONFIG%) ===
cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

REM Locate the built executable. Multi-config generators (Visual Studio) put it
REM under build\%CONFIG%\, single-config generators put it under build\.
set "EXE="
if exist "build\%CONFIG%\codemon.exe" set "EXE=build\%CONFIG%\codemon.exe"
if not defined EXE if exist "build\codemon.exe" set "EXE=build\codemon.exe"

if not defined EXE (
    echo [ERROR] Could not find codemon.exe after building.
    exit /b 1
)

REM The game loads maps\ and assets\ relative to the working directory; the
REM CMake build stages copies of them next to the executable, so run from there.
for %%I in ("%EXE%") do set "EXE_DIR=%%~dpI"
for %%I in ("%EXE%") do set "EXE_NAME=%%~nxI"

echo === Starting Cod-e-mon ===
pushd "%EXE_DIR%"
".\%EXE_NAME%"
set "RC=%errorlevel%"
popd

endlocal & exit /b %RC%
