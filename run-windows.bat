@echo off
REM ===========================================================================
REM  Cod-e-mon - Windows launcher
REM
REM  Configures and builds the project with CMake, then starts the game.
REM  By default it uses the MSVC SFML bundled under codemon\SFML linked
REM  statically, so no SFML DLLs are needed. To use your own SFML instead,
REM  set SFML_DIR before running, e.g.:
REM      set SFML_DIR=C:\SFML\lib\cmake\SFML
REM
REM  Requirements: CMake and a C++ toolchain such as Visual Studio Build Tools.
REM  Usage:  run-windows.bat            Release build, then run
REM          run-windows.bat Debug      Debug build, then run
REM
REM  The window is kept open on exit so you can read any error messages.
REM ===========================================================================
setlocal enableextensions

REM Always operate from the directory this script lives in.
cd /d "%~dp0"

REM Build configuration - Release by default.
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

echo === Cod-e-mon Windows launcher ===
echo Working directory: %CD%
echo Configuration:     %CONFIG%
echo.

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake was not found on PATH.
    echo         Install it from https://cmake.org/download/ and enable "Add CMake to PATH",
    echo         then run this script again.
    goto :fail
)

echo === Step 1/3: Configuring with CMake ===
cmake -S . -B build
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed - see the messages above.
    echo         You most likely need a C++ toolchain. Install "Visual Studio Build Tools"
    echo         with the "Desktop development with C++" workload, then run this again.
    goto :fail
)

echo.
echo === Step 2/3: Building %CONFIG% ===
cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed - see the messages above.
    goto :fail
)

REM Locate the built executable. Multi-config generators such as Visual Studio
REM put it under build\%CONFIG%\, single-config generators put it under build\.
set "EXE="
if exist "build\%CONFIG%\codemon.exe" set "EXE=build\%CONFIG%\codemon.exe"
if not defined EXE if exist "build\codemon.exe" set "EXE=build\codemon.exe"

if not defined EXE (
    echo.
    echo [ERROR] Build reported success but codemon.exe was not found.
    goto :fail
)

REM The game loads maps\ and assets\ relative to the working directory; the
REM CMake build stages copies of them next to the executable, so run from there.
for %%I in ("%EXE%") do set "EXE_DIR=%%~dpI"
for %%I in ("%EXE%") do set "EXE_NAME=%%~nxI"

echo.
echo === Step 3/3: Starting %EXE% ===
pushd "%EXE_DIR%"
".\%EXE_NAME%"
set "RC=%errorlevel%"
popd

if not "%RC%"=="0" (
    echo.
    echo [WARN] The game exited with code %RC%.
    goto :fail
)

echo.
echo Done. The game window has closed.
echo Press any key to close this window . . .
pause >nul
exit /b 0

:fail
echo.
echo Press any key to close this window . . .
pause >nul
exit /b 1
