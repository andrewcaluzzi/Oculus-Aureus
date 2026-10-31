Oculus Aureus @VERSION@@IF linux@ (Linux)@END@
built from @COMMIT@ on @DATE@

An experimental fork of Project Midas (the PC port of the GoldenEye 007 decompilation) with Lua
modding and its own game modes, all started from one main menu.
https://github.com/andrewcaluzzi/Oculus-Aureus
@IF linux@

NEEDS
A 64-bit x86 Linux with 32-bit (i386) libraries: the game is a 32-bit program, like the
Windows one. glibc 2.35 or newer (Ubuntu 22.04, Mint 21, Debian 12, Fedora 36, or newer;
Arch; SteamOS 3). Install the 32-bit SDL2 and OpenGL once:
  Ubuntu, Mint, Debian:  sudo dpkg --add-architecture i386
                         sudo apt update
                         sudo apt install libsdl2-2.0-0:i386 libgl1:i386
  Fedora:                sudo dnf install SDL2.i686 mesa-libGL.i686
  Arch:                  enable [multilib] in /etc/pacman.conf, then
                         sudo pacman -S lib32-sdl2 lib32-mesa
  Steam Deck (SteamOS):  nothing to install (untested; please report back)
If the system has no 32-bit SDL2, the launcher uses the copy in lib/ (X11 and ALSA only).
@END@

SETUP
@IF windows@
1. Put your own GoldenEye 007 (USA) cart image in the Data folder as Data\baserom.u.z64
   (see Data\README.txt). It is not included and never will be.
2. Double-click Play Oculus Aureus.bat. It opens the main menu. Running ProjectMidas.exe
   directly plays the normal game.
@END@
@IF linux@
1. Put your own GoldenEye 007 (USA) cart image in the Data folder as Data/baserom.u.z64
   (see Data/README.txt). It is not included and never will be.
2. Run the launcher from a terminal in this folder:  ./"Play Oculus Aureus.sh"
   (or let your file manager run executable scripts). It opens the main menu. Running
   ./ProjectMidas directly plays the normal game.
@END@

THE MAIN MENU
Pick a tile; each mode's own start menu opens on the same screen, and its MAIN MENU button
comes back. SETTINGS has sound, mouse, view and HUD options. EXIT closes the game.

  ARENA        Wave survival in a generated colosseum, or on the seven stock multiplayer
               maps. 20 waves with a boss every 5, endless, chaos (wave mutators and elite
               enemies), whacky effects, and co-op with a bot partner who revives you. Guns
               drop from enemies, ammo and armour appear in the middle, and kills give back
               health. The crowd keeps a favour meter: stylish kills please it.
               Best scores and medals are kept per preset (solo).
  HEIST        Plan the job in the van, then rob Janus Bank: key cards, cameras, guards,
               hostages, the vault. Or Le Grand Casino. USE (E) searches a knocked-out guard
               and crawls through vents. A debrief scores the take.
  SNIPER DUEL  A PROTOTYPE. One rifle against named marksmen on the far slopes, five rounds.
               Four places: Severnaya Ridge and Surface (the first version of the duel: watch
               for the glint), Snowfield and Jungle (the second version: hunters 250 to 500 m
               out, who spot you before they glint). On Snowfield and Jungle: E goes prone,
               Q is thermal vision, the mouse wheel swaps the rifle and the binoculars (hold
               them on a marksman to tag him). Snowstorms roll over the snowfield.

CONTROLS (keyboard and mouse; controllers work too)
  Move W A S D, look with the mouse, fire left button, aim right button (W / S zoom while
  aiming), USE E, reload R, walk Left Shift, weapons mouse wheel, items Q and [, pause Return.
  Menus: arrow keys or the mouse, Return / click to pick, Escape or B to go back.
  Rebind in Data@SEP@Devices@SEP@kbm.gedevice (midas_ini_help.txt has the list).

KNOWN ISSUES
  - Sniper Duel is a prototype: Ridge and Surface run the older duel, so the card mixes two
    styles. Severnaya Ridge has a dark sky.
  - A full five-round ladder has not been played through on every stage.
@IF linux@
  - If levels draw as a few large blank triangles (seen on some Linux systems), start it with
      GE_FULLHEIGHT=0 GE_EDGEINSET=1 ./"Play Oculus Aureus.sh"
  - The Linux build has had less testing than the Windows one.
@END@
  - Co-op, netplay and the other experimental features in the source are not in the menus
    except the Arena's bot partner.

FILES
Settings are in midas.ini, written on first run (midas_ini_help.txt has every setting).
Saves and best scores go under Data@SEP@Saves@IF windows@; the run log is Data\Logs\ge_direct.log@END@@IF linux@; the run log goes to the terminal@END@.
The mode scripts are plain Lua in Data@SEP@Scripts@SEP@modes; README-FORK.md has the details.
LICENSE.txt covers the Project Midas code; third-party code keeps its own license, see
THIRD_PARTY_NOTICES.txt@IF linux@ (lib/libSDL2-2.0.so.0 is SDL2, zlib license)@END@.
