# Changelog

Format: [semantic versioning](https://semver.org/). While the number starts with 0, the project
is in development and anything may still change.

## [0.12.0] — X is a right click

- X now makes a right click where you aim: drink a potion, use an item, like the right mouse
  button of the original game. The keyboard opens from the wrist menu.

## [0.11.0] — Classic screen and keyboard in the wrist menu

- The wrist menu gains two lines: "Classic screen" (the whole game image on the big screen, like
  the original, instead of floating windows) and "Show keyboard" / "Hide keyboard". X still opens
  the keyboard too.
- First launch: a small note under your gaze says to look at your left hand, the gear beats gently
  and the left controller buzzes lightly every few seconds. All of it stops for good once you have
  opened the menu.

## [0.10.1] — The big screen goes all the way down

- Fixed: the larger the main screen, the higher it stayed above the floor, as if an invisible
  barrier rose with its size. The screen now goes down until its frame almost touches the floor,
  whatever its size.
- The screen can be aimed lower when you drag it.
- When the full image replaces the dungeon view (menus, scenes), the screen moves up if needed so
  it never sinks into the floor.

## [0.10.0] — Wrist menu, English, no more table

- The reading table and its stool are gone from the library: the middle of the room is free.
- Look at your left hand: a small gear appears. Click it to open a menu with the language
  (English or French) and the controller mapping. The help panel no longer opens by itself.
- The app is now in English by default (keyboard, help, messages); French is one click away.
  The documentation and the wiki are in English too.
- The two paintings on the left wall are further apart.

## [0.9.0] — Help, movable table, six pictures

- Help panel with the controller mapping: shown at first launch, then from the "Help" key of the
  keyboard (X).
- The character selected for combat is simply framed in yellow (no more yellow veil).
- The table can be moved (bar above its top) and resized (right stick while holding it); its
  place is remembered.
- Six personal pictures instead of four: `tableau3` (left wall) and `tableau4` (back wall, right
  of the door) are added.

## [0.8.0] — Character sheet on the screen, optional table

- A character sheet is shown in full on the screen (which widens to the right); the windows stay
  in place and the compass window goes dark. Replaces the whole-image switch of 0.7.1, which now
  only applies to the ScummVM menu.
- "Table" button under the keyboard (X): shows or removes the table and stool.
- The "Reset all" button is now called "Reset".

## [0.7.1] — Character sheet and ScummVM menu as a whole image

- Opening a character sheet brings the whole image back on the screen (the sheet is no longer
  split between the screen and the compass window); the windows come back afterwards.
- Same for the ScummVM menu (menu button of the left controller).

## [0.7.0] — Button combat

- In the dungeon, A makes the selected character attack and B opens their magic; flicking the
  right stick up or down changes character (their frame lights up in the party window).
  Elsewhere, A and B stay Enter and Escape.

## [0.6.0] — Independent windows, corners to resize

- Each window (party, inventory, spells, controls, compass) is independent from the screen:
  moving or resizing the screen no longer moves the others. The layout starts fresh once.
- Resize the screen or a window by pulling a corner (a brass bracket appears when you aim at the
  corner, just outside).
- The move bars of the game windows are on their side (no longer above): the pointer goes from
  the screen to the party and the inventory without crossing a bar.
- Where you put the screen is remembered between sessions.
- Personal pictures: more flexible names (`castle`, `king`… or any name), and two lines under the
  keyboard (X) that tell which pictures are shown and which files the app sees in `decor`.

## [0.5.0] — Detached interface, fixes

- No more stray light on the game image (scenery glows are erased over the screen).
- Outdoor sounds (wind, drips, crickets) are removed; only the fire remains, off by default.
- The yellow aiming beam is fully visible when pointing off the screen; larger move handles.
- Windows (party, inventory, spells, controls, compass) placed in the screen plane, attached to
  it, and no longer going through the floor, walls or table; no more duplicate atlas window.
- Left stick: one direction at a time (easier strafing).
- Personal pictures: window, portrait, `tableau1` and `tableau2`, detected by name prefix in the
  `decor` folder, without restarting the app.

## [0.4.0] — Movable windows

- Interface detached from the screen: inventory, spells, portraits, compass and controls in
  windows you grab (trigger on the handle) and place wherever you like.
- Virtual keyboard (X button) to name saves.
- Library rearranged: table in front of the player, relics on pedestals, king's portrait, window
  view.
- Movable and adjustable screen in mixed reality; 486 PC under the CRT monitor.
- Fire sound with an on/off switch.

## [0.3.0] — Window mode (dropped)

- Enlarged dungeon view with a wrist interface. Not convincing; replaced by the windows of 0.4.0.

## [0.2.0] — Library and mixed reality

- Medieval library around the screen, changing with the game area.
- Ornate frame, gloves, CRT monitor, mixed reality, haptics, spatial sound.

## [0.1.0] — Floating screen

- The full game on a floating screen in VR on Meta Quest 3, with controllers as pointer, sound
  and saves.
