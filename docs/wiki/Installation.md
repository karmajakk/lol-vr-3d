# Installation

Nothing to compile.

## 1. Install the app

1. Download `lol-vr-3d.apk` from the [latest release](https://github.com/karmajakk/lol-vr-3d/releases/latest).
2. Connect the headset to your PC, open [SideQuest](https://sidequestvr.com) and install the APK (the "Install APK file from folder" icon).
3. In the headset: Library → "Unknown sources" filter → **Lands of Lore VR**.

At first launch, a window asks whether you want the **classic view** or the **immersive mode** (you can change it later: floppy on your left wrist, then "VR controls"), and a screen says the game files are missing: that's expected. This launch creates the folder where they go.

## 2. Copy the game files

Copy **the whole content of the game CD** (the `.PAK` files, including `GENERAL.PAK`) into this headset folder:

`Android/data/com.karmajakk.lolvr3d/files/lol`

Use SideQuest's file manager, or Windows Explorer once the headset is connected and file access is allowed.

**GOG version**: the `.PAK` files are packed inside `GAME.DAT`, a CD image. Open `GAME.DAT` with [7-Zip](https://www.7-zip.org) and copy the content of its `DATA` folder (including the `ENG` subfolder).

The game starts by itself as soon as the files are in place.

If you used the older `lol-vr` app: this app is installed next to it and has its own folders. Copy the game files a second time, and your saves from `Android/data/com.karmajakk.lolvr/files/sauvegardes` if you want to keep them.

## Language

The app follows the language chosen in the game's options: floppy on your left wrist, "Game controls", "Language is". Before the game has started, it uses the last language seen (English at first launch).

## Updating

Just install the new APK **over** the old one: the game files and the saves are kept.

## Saves

They are in `Android/data/com.karmajakk.lolvr3d/files/sauvegardes`.

**Warning: uninstalling the app erases this folder.** Copy it to your PC before uninstalling.
