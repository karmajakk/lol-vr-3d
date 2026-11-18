# Lands of Lore VR 3D (lol-vr-3d)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) on Meta Quest 3, standalone, with the
dungeon rebuilt **in true 3D around you**. The original game still runs on
[ScummVM](https://www.scummvm.org) and decides everything (combat, items, spells, saves); this app
reads the level the game has loaded and draws its walls, floor and ceiling at full scale around you.

This is the sister project of [lol-vr](https://github.com/karmajakk/lol-vr), which shows the game on
a floating screen in a medieval library. Both apps can be installed side by side.

**The game files are not included.** You need your own copy of the game (CD or GOG version).

## Status

Early demo. What works:

- The walls of every level, taken from your own game files, at real size around you, with their
  decorations and doors drawn by the game engine itself.
- Floor and ceiling rebuilt from the game's own background.
- The party's moves and turns glide smoothly; doors that open or close update the walls.
- Everything from lol-vr: the game screen, the detached party, inventory and compass windows,
  controllers, keyboard, wrist menu (gear on the left hand).
- "3D dungeon: on/off" in the wrist menu, to compare with the original view.

Not there yet: monsters and items in 3D, doors that slide open in 3D (they jump from closed to
open), outdoor skies. Until then, the game screen shows them.

Version details: [CHANGELOG.md](CHANGELOG.md).

## Installation (nothing to compile)

1. Download `lol-vr-3d.apk` from the
   [latest release](https://github.com/karmajakk/lol-vr-3d/releases/latest).
2. Connect the headset to your PC, open [SideQuest](https://sidequestvr.com) and install the APK.
3. In the headset, launch **Lands of Lore VR 3D** (Library → "Unknown sources" filter).
   A screen tells you the game files are missing: that's expected. This first launch creates
   the folder where they go.
4. On the PC, copy the game files (the `.PAK` files, including `GENERAL.PAK`, and the `ENG`
   folder) into this folder of the headset:

   `Android/data/com.karmajakk.lolvr3d/files/lol`

   It is a different folder from lol-vr's: copy the same files there a second time.
   **GOG version:** open `GAME.DAT` with [7-Zip](https://www.7-zip.org) and copy the content of
   its `DATA` folder.
5. The game starts by itself as soon as the files are in place.

Saves are kept in `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`. You can copy your lol-vr
saves there.

## Controls

Same as lol-vr: aim and pull the trigger to click, grip for a right click, left stick to walk,
right stick to turn, A to attack, B for magic, Y for the map, X for the keyboard. Look at your left
hand and click the gear for the menu (language, classic screen, 3D dungeon, keyboard, controller
mapping).

In the dungeon, the party's position is under your feet and its direction is straight ahead of you:
when the party turns, the dungeon turns around you.

## How it works (for the curious)

The engine is patched (`patches/0003-niveau-pour-le-donjon-3d.patch`) to publish the current
level: the wall type of each side of the 32 x 32 blocks and the party's block and direction. When a
level loads, the engine also draws each wall type the way it shows a wall right in front of the
party (128 x 96 pixels, decorations and doors included); those pictures dress the 3D walls. The
floor and ceiling come from the game's background picture.
Format notes: [KForestland/lands-of-lore-1-re](https://github.com/KForestland/lands-of-lore-1-re)
and the ScummVM source (`engines/kyra/engine/scene_rpg.cpp`).

## Building

Pushes are built by GitHub Actions (`.github/workflows/build.yml`, `scripts/build.sh`). A version
is published when the `VERSION` file holds a number without a release.

## License

GPL-3.0, like ScummVM (see `COPYING`).
