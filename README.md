# Lands of Lore VR

**English** · [Français](README.fr.md) · [Deutsch](README.de.md)

*Lands of Lore: The Throne of Chaos* (Westwood, 1993) on Meta Quest 3, standalone, no PC needed.
The original game runs on [ScummVM](https://www.scummvm.org) and decides everything (combat,
items, spells, saves). Two ways to play, switched at any time from "VR controls" in the game menu (the floppy on your left wrist):

- **Immersive mode**: the dungeon rebuilt **in true 3D around you**, at full scale, from the level
  the game has loaded. One view only; the game screen comes up when needed (shops, conversations,
  the map, menus).
- **Classic view**: the game on a floating screen in a medieval library, in your own room (mixed
  reality) or on an old 486 PC, with the interface in movable windows.

At first launch, a window asks which one you want, and your language (English, French or German).

This project now includes [lol-vr](https://github.com/karmajakk/lol-vr), where the classic view
was first developed (its history is kept here; its changelog is in
[docs/lol-vr-CHANGELOG.md](docs/lol-vr-CHANGELOG.md)). Documentation: [docs/wiki](docs/wiki/Home.md).

**The game files are not included.** You need your own copy of the game (CD or GOG version).

## Status

Early demo. What works:

- The walls of every level, taken from your own game files, at real size around you, with their
  decorations and doors drawn by the game engine itself.
- Floor and ceiling rebuilt from the game's own background.
- The party's moves and turns glide smoothly; doors that open or close update the walls.
- Everything from lol-vr: the game screen, the detached windows, controllers, keyboard, wrist
  floppy (game menu) on the left hand. The windows are arranged for the 3D dungeon: on the left the spell
  scroll with the message line joined under it; on the right the characters one under the other,
  the atlas column (lamp, purse) beside them with the rest button under the purse, in one window;
  the inventory below.
- Shop doors and passages to other regions stay in front of you, like a wall with their picture:
  walk into them and the game screen shows the shop, or the new region appears.
- Dark places stay dark until you light the lamp.
- "VR mode" in "VR controls" (game menu): Immersive, Classic (library) or Mixed reality; in the last
  two, L3 switches the view: big screen, windows, 486 PC.
- Conversations: the game's large text box, with the speaker's portrait, is joined under the game
  screen.
- Monsters and items near the party, drawn by the game itself and always facing you.
- Clicks in the 3D dungeon: on the walls around the party (door buttons, levers, keyholes, bushes,
  nests) and on the floor in front of you (items).
- Spell effects are shown in 3D, as the game draws them. The game screen appears by itself for
  scenes, dialogues, the map and menus.

- Doors rise into their frame step by step, like in the game; thrown items and projectiles fly
  through the 3D dungeon at the height the game gives them.

Version details: [CHANGELOG.md](CHANGELOG.md).

## Installation (nothing to compile)

1. Download `lol-vr-3d.apk` from the
   [latest release](https://github.com/karmajakk/lol-vr-3d/releases/latest).
2. Connect the headset to your PC, open [SideQuest](https://sidequestvr.com) and install the APK.
3. In the headset, launch **Lands of Lore VR 3D** (Library → "Unknown sources" filter) and choose
   the classic view or the immersive mode.
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

Same as lol-vr: aim and pull the trigger to click, X (or the grip) for a right click (drink a
potion, use an item), left stick to walk, right stick to turn, A to attack, B for magic, Y for the
map. Look at your left hand and click the floppy for the game menu; in it, "VR controls" holds the
headset settings (VR mode, the view in Classic and Mixed reality, controller mapping). The app speaks the language set in the game's options (English, French or German). The
keyboard comes up by itself to name a save.

In the dungeon, the party's position is under your feet. You walk where you look: the left stick
points relative to your gaze (forward is where you look, sideways is a side step), and when you
stand still the party quietly turns to face where you look. Turning with the right stick is
instant, with a very short fade, to stay comfortable.

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
is published when the `VERSION` file holds a number without a release. To test on a Linux PC
without the headset, see [scripts/dev](scripts/dev/README.md).

## License

GPL-3.0, like ScummVM (see `COPYING`).
