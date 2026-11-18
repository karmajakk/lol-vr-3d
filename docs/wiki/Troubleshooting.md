# Troubleshooting

**The screen says the game files are missing.**
Expected at first launch. Copy the CD content into `Android/data/com.karmajakk.lolvr3d/files/lol` (see [Installation](Installation.md)). With the GOG version, open `GAME.DAT` with 7-Zip first.

**The app is in English, I want French (or the other way round).**
Look at your left hand, click the gear, then "Language".

**I don't remember the buttons.**
Look at your left hand, click the gear, then "Controller mapping". See also [Controls](Controls.md).

**My pictures (window, portrait) don't show up.**
Open the keyboard (wrist menu) and read the two lines under the keys: they tell which pictures are displayed and which files the app sees. "Folder 'decor' is empty" means the files are not in `Android/data/com.karmajakk.lolvr3d/files/decor`; "Unreadable" means the format isn't supported (use PNG or JPEG, not WebP or HEIC). See [Scenery and pictures](Scenery-and-pictures.md).

**The windows or the screen are badly placed.**
Click the right stick to recenter, or X → "Reset layout".

**A reflection or stray light on the game image.**
Report it in the [Issues](https://github.com/karmajakk/lol-vr-3d/issues), saying where on the screen.

**Strafing is hard.**
The left stick gives one direction at a time, the one it leans the most toward; push it clearly sideways rather than diagonally.

**I lost my saves.**
Uninstalling the app erases the `sauvegardes` folder. To update, install the APK over the old one without uninstalling.

**I can't hear the fire.**
It's off by default: X button → "Fire".

**Something else.**
Open an [Issue](https://github.com/karmajakk/lol-vr-3d/issues) with the version (see the [Releases](https://github.com/karmajakk/lol-vr-3d/releases) page) and what you were doing.
