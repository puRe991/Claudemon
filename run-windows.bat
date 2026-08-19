@echo off
REM ===========================================================================
REM  Cod-e-mon - Windows launcher (thin wrapper)
REM
REM  Hands off to scripts\windows-setup.ps1, which checks the build
REM  dependencies, installs any that are missing (via winget), then builds and
REM  starts the game. Kept deliberately tiny and label-free so line-ending
REM  quirks cannot break it. The PowerShell script keeps the window open.
REM
REM  Usage:  run-windows.bat            Release build, then run
REM          run-windows.bat Debug      Debug build, then run
REM ===========================================================================
setlocal enableextensions
cd /d "%~dp0"

where powershell >nul 2>nul
if errorlevel 1 (
    echo [ERROR] Windows PowerShell was not found; this launcher needs it.
    echo Press any key to close . . .
    pause >nul
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\windows-setup.ps1" -Config "%~1"
set "RC=%errorlevel%"

REM Fallback pause in case PowerShell itself failed to start and never paused.
if not "%RC%"=="0" (
    echo.
    echo The launcher exited with code %RC%. See the messages above.
    echo Press any key to close . . .
    pause >nul
)

exit /b %RC%
