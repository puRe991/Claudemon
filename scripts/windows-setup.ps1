<#
    Cod-e-mon - Windows setup & launcher

    Checks for the build dependencies, offers to install any that are missing
    (via winget), then configures, builds and starts the game with CMake.

    Invoked by run-windows.bat. Can also be run directly:
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-setup.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File scripts\windows-setup.ps1 -Config Debug
        ... -SkipInstall     (only check, never install)
#>
[CmdletBinding()]
param(
    [string]$Config = "Release",
    [switch]$SkipInstall
)

if (-not $Config) { $Config = "Release" }

# Repo root is the parent of this script's folder (scripts\..).
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Write-Head($text) {
    Write-Host ""
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

function Have-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Refresh-Path {
    # Pick up PATH changes made by installers in the current session.
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user    = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = ($machine, $user | Where-Object { $_ }) -join ";"
}

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p  = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
}

function Find-Msvc {
    # Returns $true if a C++ (VC) toolset is installed, via vswhere.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $path = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    return [bool]$path
}

function Pause-Exit($code) {
    Write-Host ""
    Read-Host "Press Enter to close this window"
    exit $code
}

Write-Host "============================================" -ForegroundColor Green
Write-Host "  Cod-e-mon Windows setup & launcher"        -ForegroundColor Green
Write-Host "  Repo:   $RepoRoot"
Write-Host "  Config: $Config"
Write-Host "============================================" -ForegroundColor Green

# ---------------------------------------------------------------------------
# 1. Work out which dependencies are missing.
# ---------------------------------------------------------------------------
Write-Head "Checking dependencies"

Refresh-Path
$needCMake = -not (Have-Command "cmake")
$needMsvc  = -not (Find-Msvc)

Write-Host ("  CMake ............ {0}" -f ($(if ($needCMake) { "MISSING" } else { "ok" })))
Write-Host ("  C++ toolchain .... {0}" -f ($(if ($needMsvc)  { "MISSING" } else { "ok" })))

$needInstall = $needCMake -or $needMsvc

if ($needInstall -and $SkipInstall) {
    Write-Host ""
    Write-Host "[ERROR] Missing dependencies and -SkipInstall was given." -ForegroundColor Red
    Pause-Exit 1
}

# ---------------------------------------------------------------------------
# 2. Install missing dependencies via winget (needs admin).
# ---------------------------------------------------------------------------
if ($needInstall) {
    if (-not (Have-Command "winget")) {
        Write-Host ""
        Write-Host "[ERROR] Dependencies are missing and winget is not available to install them." -ForegroundColor Red
        Write-Host "        Install 'App Installer' from the Microsoft Store (which provides winget),"
        Write-Host "        or install the following manually, then re-run this script:"
        if ($needCMake) { Write-Host "          - CMake           https://cmake.org/download/" }
        if ($needMsvc)  { Write-Host "          - VS Build Tools  https://visualstudio.microsoft.com/downloads/ (Desktop development with C++)" }
        Pause-Exit 1
    }

    # Installing machine-wide needs elevation. If we are not admin, relaunch
    # this same script elevated and let that instance finish the job.
    if (-not (Test-Admin)) {
        Write-Host ""
        Write-Host "Installing dependencies requires administrator rights." -ForegroundColor Yellow
        Write-Host "Re-launching elevated (accept the UAC prompt)..."
        try {
            Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList @(
                "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", "`"$PSCommandPath`"",
                "-Config", $Config
            )
        } catch {
            Write-Host "[ERROR] Elevation was cancelled or failed: $($_.Exception.Message)" -ForegroundColor Red
            Pause-Exit 1
        }
        # The elevated window takes over from here.
        exit 0
    }

    if ($needCMake) {
        Write-Head "Installing CMake (winget)"
        winget install --id Kitware.CMake -e --source winget `
            --accept-package-agreements --accept-source-agreements
        Refresh-Path
        if (-not (Have-Command "cmake")) {
            # winget doesn't refresh PATH for the running session; add the default location.
            $cmakeBin = Join-Path $env:ProgramFiles "CMake\bin"
            if (Test-Path (Join-Path $cmakeBin "cmake.exe")) { $env:Path = "$cmakeBin;$env:Path" }
        }
        if (-not (Have-Command "cmake")) {
            Write-Host "[ERROR] CMake still not found after install. Close and reopen the launcher." -ForegroundColor Red
            Pause-Exit 1
        }
    }

    if ($needMsvc) {
        Write-Head "Installing Visual Studio Build Tools with the C++ workload (winget)"
        Write-Host "This is a large download (several GB) and can take a while." -ForegroundColor Yellow
        winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget `
            --accept-package-agreements --accept-source-agreements `
            --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        if (-not (Find-Msvc)) {
            Write-Host "[ERROR] C++ toolchain still not detected after install." -ForegroundColor Red
            Write-Host "        A reboot is sometimes required; reboot and run the launcher again." -ForegroundColor Red
            Pause-Exit 1
        }
    }

    Write-Host ""
    Write-Host "All dependencies are installed." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# 3. Configure, build and run.
# ---------------------------------------------------------------------------
Push-Location $RepoRoot
try {
    Write-Head "Step 1/3: Configuring with CMake"
    cmake -S . -B build
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] CMake configuration failed - see the messages above." -ForegroundColor Red
        Pause-Exit 1
    }

    Write-Head "Step 2/3: Building $Config"
    cmake --build build --config $Config
    if ($LASTEXITCODE -ne 0) {
        # The bundled MSVC SFML may not match the local toolset. Rebuild SFML
        # from source so it links against exactly this compiler.
        Write-Host ""
        Write-Host "[WARN] Build failed with the bundled SFML. Retrying by building SFML" -ForegroundColor Yellow
        Write-Host "       from source (this downloads and compiles SFML; slower first time)..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force (Join-Path $RepoRoot "build") -ErrorAction SilentlyContinue
        cmake -S . -B build -DCODEMON_FETCH_SFML=ON
        if ($LASTEXITCODE -eq 0) {
            cmake --build build --config $Config
        }
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[ERROR] Build still failed after building SFML from source - see above." -ForegroundColor Red
            Pause-Exit 1
        }
    }

    Write-Head "Step 3/3: Starting the game"
    $exe = $null
    foreach ($candidate in @("build\$Config\codemon.exe", "build\codemon.exe")) {
        $full = Join-Path $RepoRoot $candidate
        if (Test-Path $full) { $exe = $full; break }
    }
    if (-not $exe) {
        Write-Host "[ERROR] Build succeeded but codemon.exe was not found." -ForegroundColor Red
        Pause-Exit 1
    }

    # The game loads maps\ and assets\ relative to the working directory; the
    # CMake build stages copies of them next to the executable, so run there.
    $exeDir = Split-Path -Parent $exe
    Push-Location $exeDir
    try {
        & $exe
        $rc = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($rc -ne 0) {
        Write-Host ""
        Write-Host "[WARN] The game exited with code $rc." -ForegroundColor Yellow
    } else {
        Write-Host ""
        Write-Host "The game window has closed. Done." -ForegroundColor Green
    }
}
finally {
    Pop-Location
}

Pause-Exit 0
