# package_release.ps1 - assemble the three platform release folders under ./releases
#
# Usage (from the repo root):  powershell -File scripts\port\package_release.ps1 [-Name <folder>]
#
# Layout, the same one the earlier GE-menu-overhaul-*.7z drops used:
#   releases\<name>\windows\    ProjectMidas.exe SDL2.dll gamecontrollerdb.txt gamecontrollerdefault.txt midas_ini_help.txt Data\ Mods\
#   releases\<name>\linux\      ProjectMidas (build_deck) libSDL2-2.0.so.0 (i386) gamecontrollerdb.txt gamecontrollerdefault.txt midas_ini_help.txt Data\ Mods\
#   releases\<name>\steamdeck\  the linux set + libSDL2-2.0.so.0 (i386) + run.sh
#   releases\<name>\ProjectMidas-<name>-{windows,linux,steamdeck}.zip
#
# Name it after the version for a release, e.g. -Name v1.0.2b gives
# ProjectMidas-v1.0.2b-windows.zip. Zips, not 7z: some players could not open 7z.
#
# RUNTIME payload only: never a ROM, midas.ini, eeprom.bin, gamecontrolleruser.txt
# or logs. The launcher is NOT bundled. Build first - this
# does not compile:
#   wsl -e bash -lc "cmake --build build_win -j 8"      (ProjectMidas.exe, mingw i686)
#   wsl -e bash -lc "cmake --build build_linux -j 8"    (ge, Linux i686)
#   wsl -e bash -lc "cmake --build build_deck -j 8"     (ge, Linux i686 + RUNPATH $ORIGIN, so it
#                                                         finds the bundled SDL however it is started; G220)
param(
    [string]$Name = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($Name -eq "") {
    $branch = (git -C $root rev-parse --abbrev-ref HEAD).Trim()
    $Name = (Get-Date -Format "yyyy-MM-dd") + "-" + $branch
}
$out = Join-Path (Join-Path $root "releases") $Name
$hash = (git -C $root rev-parse --short HEAD).Trim()
# tracked files only: build folders and other untracked files are not part of the build
$dirty = if ((git -C $root status --porcelain --untracked-files=no).Length -gt 0) { " (+ uncommitted changes)" } else { "" }
$stamp = "built from $hash$dirty on $(Get-Date -Format 'yyyy-MM-dd HH:mm')"

foreach ($f in @("build_win\ProjectMidas.exe", "build_win\SDL2.dll", "build_linux\ProjectMidas", "build_deck\ProjectMidas",
                 "tools\gamecontrollerdb.txt", "tools\gamecontrollerdefault.txt",
                 "tools\Data_README.txt", "tools\Mods_README.txt", "tools\midas_ini_help.txt", "tools\shaders_base.txt",
                 "LICENSE", "tools\THIRD_PARTY_NOTICES.txt")) {
    if (-not (Test-Path (Join-Path $root $f))) { throw "missing $f - build first" }
}
# G138: the README stamp below names HEAD, so every binary must have been built
# at HEAD. Each build dir's generated stamp says which commit it was built from
# (PORT_BUILD_COMMIT; older stamps only have the commit count).
$wantCount = (git -C $root rev-list --count HEAD).Trim()
foreach ($b in @("build_win", "build_linux", "build_deck")) {
    $hdr = Join-Path $root "$b\generated\port_buildinfo.h"
    if (-not (Test-Path $hdr)) { throw "$b has no generated\port_buildinfo.h - rebuild it first" }
    $txt = Get-Content -Raw $hdr
    if ($txt -match '#define PORT_BUILD_COMMIT "([0-9a-f]+)"') {
        if (-not "$(git -C $root rev-parse HEAD)".StartsWith($Matches[1])) { throw "$b was built at commit $($Matches[1]), HEAD is $hash - rebuild it first" }
    } elseif ($txt -match '#define PORT_BUILD_NUMBER ([0-9]+)') {
        if ($Matches[1] -ne $wantCount) { throw "$b was built at build $($Matches[1]), HEAD is $wantCount - rebuild it first" }
    }
}
# the i386 SDL2 the deck bundle carries: the PORTABLE self-built one (2026-10-14:
# Ubuntu's hard-links libsamplerate, libdecor, wayland... that SteamOS lacks in
# 32-bit - "error loading shared library libsamplerate.so.0"). SDL2 2.30.11 -m32
# with X11/ALSA/PulseAudio dlopen'd; it NEEDS only libc + libm. Built in WSL at
# ~/sdl2deck (the recipe is the CI's SDL2 step with SDL_PULSEAUDIO(_SHARED)=ON).
$sdlso = Join-Path $env:TEMP "libSDL2-2.0.so.0"
wsl -e bash -lc "cp `$(readlink -f ~/sdl2deck/inst/lib/libSDL2-2.0.so.0) '$(($sdlso -replace '\\','/') -replace '^C:','/mnt/c')' && strip --strip-unneeded '$(($sdlso -replace '\\','/') -replace '^C:','/mnt/c')'"
if (-not (Test-Path $sdlso)) { throw "could not fetch ~/sdl2deck/inst/lib/libSDL2-2.0.so.0 from WSL - build the portable SDL2 first" }
# G288: ge and that SDL need the BUILD host's glibc (2.38 on Ubuntu 24.04), not
# "any Linux" - read the real floor from their imports and say it in the bundles
$wslge = ((Join-Path $root "build_linux\ProjectMidas") -replace '\\','/') -replace '^C:','/mnt/c'
$wslso = ($sdlso -replace '\\','/') -replace '^C:','/mnt/c'
$glibc = "$(wsl -e bash -lc "objdump -T '$wslge' '$wslso' 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1")".Trim() -replace '^GLIBC_',''
$glibcNote = if ($glibc -ne "") { "glibc $glibc or newer" } else { "the build machine's glibc or newer" }

if (Test-Path $out) { Remove-Item -Recurse -Force $out }

function Common([string]$dir) {
    New-Item -ItemType Directory -Force (Join-Path $dir "Data\Textures\Base") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $dir "Data\Textures\Options") | Out-Null
    New-Item -ItemType Directory -Force (Join-Path $dir "Mods") | Out-Null
    Copy-Item (Join-Path $root "tools\gamecontrollerdb.txt")      $dir
    Copy-Item (Join-Path $root "tools\gamecontrollerdefault.txt") $dir
    Copy-Item (Join-Path $root "tools\Data_README.txt") (Join-Path $dir "Data\README.txt")
    Copy-Item (Join-Path $root "tools\shaders_base.txt") (Join-Path $dir "Data\shaders_base.txt")   # shader prewarm list (scripts/port/record_shaders.sh)
    Copy-Item (Join-Path $root "tools\Mods_README.txt") (Join-Path $dir "Mods\README.txt")
    Copy-Item (Join-Path $root "tools\midas_ini_help.txt")  $dir
    Copy-Item (Join-Path $root "LICENSE") (Join-Path $dir "LICENSE.txt")
    Copy-Item (Join-Path $root "tools\THIRD_PARTY_NOTICES.txt") $dir
    "Project Midas - $Name`r`n$stamp`r`n`r`nProject Midas is a PC port of GoldenEye 007. It needs your own GoldenEye 007 (USA) cart image.`r`n`r`n1. Put the cart image in the Data folder (see Data/README.txt).`r`n2. Run the game.`r`n`r`nSettings are in midas.ini, written on first run. Delete it to restore the defaults. See midas_ini_help.txt for every setting.`r`n`r`nLICENSE.txt covers the Project Midas code. Third-party code keeps its own license; see THIRD_PARTY_NOTICES.txt.`r`n" |
        Set-Content -Encoding ASCII (Join-Path $dir "README.txt")
}

$win = Join-Path $out "windows"; Common $win
Copy-Item (Join-Path $root "build_win\ProjectMidas.exe") $win
Copy-Item (Join-Path $root "build_win\SDL2.dll") $win

$lin = Join-Path $out "linux"; Common $lin
# 2026-10-18: a Linux user got 'libSDL2-2.0.so.0 missing' (a 64-bit distro has no
# i386 SDL2). Ship the deck's RUNPATH $ORIGIN exe and the portable SDL2 here too.
Copy-Item (Join-Path $root "build_deck\ProjectMidas") $lin
Copy-Item $sdlso (Join-Path $lin "libSDL2-2.0.so.0")
"Needs a 32-bit-capable Linux with $glibcNote. SDL2 is bundled.`r`nIf ./ProjectMidas says 'Permission denied': chmod +x ProjectMidas (the archive does not keep the exec bit).`r`n" | Add-Content -Encoding ASCII (Join-Path $lin "README.txt")

$deck = Join-Path $out "steamdeck"; Common $deck
Copy-Item (Join-Path $root "build_deck\ProjectMidas") $deck
Copy-Item $sdlso (Join-Path $deck "libSDL2-2.0.so.0")
"Needs $glibcNote (an older SteamOS fails with 'GLIBC_... not found').`r`nStart it with: sh run.sh  (or chmod +x run.sh ProjectMidas once - the archive does not keep the exec bit).`r`n" | Add-Content -Encoding ASCII (Join-Path $deck "README.txt")
# LF line endings: this runs on the deck
[IO.File]::WriteAllText((Join-Path $deck "run.sh"),
    "#!/bin/sh`n# Steam Deck / 32-bit-capable Linux with ${glibcNote}: run the game with the bundled SDL.`n# If the bundled SDL needs a library this system lacks, fall back to the`n# system 32-bit SDL2 (lib32-sdl2) if it has one.`ncd `"`$(dirname `"`$0`")`"`nchmod +x ./ProjectMidas 2>/dev/null`nif LD_LIBRARY_PATH=. ldd ./ProjectMidas 2>/dev/null | grep -q `"not found`"; then`n    echo `"run.sh: bundled SDL2 is missing a dependency here; using the system SDL2`" >&2`n    exec ./ProjectMidas `"`$@`"`nfi`nLD_LIBRARY_PATH=.; export LD_LIBRARY_PATH`nexec ./ProjectMidas `"`$@`"`n")

# each zip holds one top folder named like the zip; entry names use '/' so
# Linux unzip sees folders (Compress-Archive on PowerShell 5 writes '\')
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($p in @("windows", "linux", "steamdeck")) {
    $base = "ProjectMidas-$Name-$p"
    $arc = Join-Path $out "$base.zip"
    $src = Join-Path $out $p
    $zip = [IO.Compression.ZipFile]::Open($arc, [IO.Compression.ZipArchiveMode]::Create)
    try {
        Get-ChildItem -Recurse $src | ForEach-Object {
            $rel = $_.FullName.Substring($src.Length + 1) -replace '\\', '/'
            if ($_.PSIsContainer) {
                [void]$zip.CreateEntry("$base/$rel/")
            } else {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, "$base/$rel", [IO.Compression.CompressionLevel]::Optimal)
            }
        }
    } finally {
        $zip.Dispose()
    }
}

Get-ChildItem -Recurse $out | Where-Object { -not $_.PSIsContainer } |
    ForEach-Object { "{0,10}  {1}" -f $_.Length, $_.FullName.Substring($out.Length + 1) }
"-> $out"
