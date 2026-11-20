# Changelog

Format: [semantic versioning](https://semver.org/). While the number starts with 0, the project
is in development and anything may still change.

## [0.13.0] — Doors that open, items in flight

- Doors now rise into their frame step by step, as in the game, instead of jumping from closed to
  open: the engine draws each door closed and reports how far up it is (patch 0003), and the 3D
  dungeon shows the part still below the lintel, raised by that much. Before, the in-between steps
  showed no door, or a door set too far back.
- Thrown items and projectiles fly through the 3D dungeon: a throw lasts a fraction of a second,
  faster than the pictures of monsters and items are taken, so the engine now publishes every step
  of each flight and takes the flying object's picture (the game's thrown shape, seen from the
  party's side) as soon as it is launched. The app glides between the steps, a little below eye
  level, as if thrown from the hand.
- Dev tools: `scripts/dev/regen_patch.sh` regenerates patch 0003; the end-to-end bench gains
  `watch` (objects in flight, step by step), `LOLVR_FORCE_DOOR` (show each door step) and a
  shorter `click`.

## [0.12.0] — Item on the pointer, castle door, VR controls

- Immersive mode: an item taken from the inventory stays on the pointer when you aim into the
  3D dungeon (the game's own cursor, the item's icon, at the aimed point), instead of vanishing.
  The engine now publishes its mouse cursor and the item in hand (patch 0003).
- Castle doors: the door, its frame and its pillars are now set back on one plane inside the
  doorway, at the depth the game's picture gives them, instead of a narrow door behind a dark step.
- "VR controls": the "Game screen: if needed / always" line is gone in immersive mode; the game
  screen comes up when it is needed. A saved "always" setting is read as immersive mode.
- "Controller mapping" now opens over the game screen (just in front of it, same apparent size).
- The map (atlas) no longer lets the game screen flash for an instant before it: the engine only
  reports the map once it is drawn.

## [0.11.2] — A better Quebec flag

- The flags of the first-launch window are larger (96 x 64) and in the 3:2 shape of the Quebec
  flag; the Fleurdelisé follows the official proportions (cross a sixth of the width and a quarter
  of the height, a fleur-de-lis in the middle of each blue quarter, its shape taken from the official
  flag). The window is a little taller to make room, and the controller panel with it.

## [0.11.1] — Quebec flag

- The French button of the first-launch window shows the flag of Quebec (the Fleurdelisé) instead
  of the flag of France.

## [0.11.0] — Language in the first window

- The first-launch window now also asks for the language: three buttons, English, Français and
  Deutsch, each with its flag below. The choice is the app's and the game's: the game starts in
  that language (CD version, which has the three), or switches to it at once if it is already
  running. The game's own "Language is" option still works and the app follows it.
- "VR controls" in the game menu is named in the game's current language.
- Engine (patch 0003): retro_lolvr_set_lang(), applied when the engine starts or at the next turn
  of its main loop, reloading the language files as the options menu does.

## [0.10.0] — German, documentation in three languages

- The app speaks German too, when the game is set to German ("Language is" in the game's options):
  first-launch choice, hint, VR controls, view reminder, keyboard, controller panel, spellbook strip
  and messages, with the game's own umlauts and ß.
- At the very first launch, before any game language is known, the app takes the headset's language
  (English, French or German).
- The documentation comes in English (default), French and German: README.fr.md and README.de.md,
  and the wiki pages in docs/wiki/fr and docs/wiki/de, each page linking to its translations.

## [0.9.0] — VR mode, views, the game's language

- "VR controls" has a **VR mode** setting: Immersive, Classic (the medieval library) or Mixed
  reality (your real room). In Classic and Mixed reality, a **View** setting, also on L3 (short click
  of the left stick): big screen, windows, or the 486 PC. The 486 PC is now available in the library
  too, not only in mixed reality.
- The first time you pick Classic or Mixed reality, a note in front of you says that L3 changes the
  view (click it to close).
- The Language row is gone: the app now speaks the language chosen in the game's own options
  ("Language is", in Game controls). German stays in English. The game tells the app its language
  (patch 0003).

## [0.8.0] — Floppy on the wrist, VR controls in the game menu

- The left wrist shows a floppy disk instead of the gear. Clicking it opens the game's own menu
  (load, save, delete, game and audio controls, exit), as the floppy button of the game does;
  clicking it again closes the menu. The floppy is gone from the party window.
- The game menu has a new button, "VR controls" ("Réglages VR" in French), left of "Resume game":
  it closes the menu and opens the headset settings in front of you (language, immersive mode,
  game screen or classic screen, controller mapping, close). The keyboard row is gone from it: the
  keyboard comes up by itself to name a save.
- The rest button (ZZZ) now sits under the purse, in the atlas column of the party window.
- The keyboard loses the Fire button (the library's fireplace sound stays off) and the two lines
  about your scenery pictures under the keys.

## [0.7.1] — Keyboard for save names

- The keyboard opens by itself when you click a save slot and the game asks for a description, and
  closes again once the game is saved or cancelled (unless it was already open, or you closed it
  yourself meanwhile). The game says when a name is being typed (patch 0003).

## [0.7.0] — The game's own look, conversations

- The app's windows now look like the game's menus: the first-launch choice, the invitation to
  look at your left hand, the wrist menu, the keyboard, the controller panel and the spellbook
  strip use the game's own font (read from your game files) and its terracotta panels with raised
  edges, gold text and white titles, like the in-game options menu.
- The wrist menu is laid out like the game's options: the setting on the left, its value on a
  button on the right (Language, Immersive mode, Game screen or Classic screen, Keyboard), then
  "Controller mapping" and "Close". On the keyboard, Fire and Panels are pushed in, their name in
  blue, when they are on; Shift is an arrow.
- Conversations in immersive mode: when the game opens its large text box (with the portrait of
  the one speaking), the whole box is joined under the game screen instead of just its first line,
  and its choices can be clicked there. With the game's text turned off, the game draws no such box
  (voices only) and the message line stays as it was. Full-screen story scenes still show the whole
  game image.

## [0.6.1] — Remaining black wall, clicking on the sides

- A second case of the black wall at the end of a forest path, missed by the 0.5.2 fix: when the
  ordinary forest wall was decoded after the picture that closes off the path, the picture never
  got attached to it. It now always does, however the two are decoded.
- In immersive mode, the pointer now picks the walls around the party's own cell, not just the one
  blocking the way ahead: bushes, nests, holes in trees and other clickable spots on the walls
  beside you (or behind) respond to a look and a click, the same as the wall in front.

## [0.6.0] — Spells in 3D, folding spellbook

- Spell effects in immersive mode: while a spell is cast, what the game draws over its view
  (the Spark orb, fireballs, lightning, mist...) appears in the 3D dungeon, in front of the party,
  where the game shows it.
- The spellbook folds: the left window now has a strip at the top, "Spellbook +" / "-". Folded,
  only the strip and the message line remain, so you see more of the dungeon; click the strip to
  open it and change the spell. The message line stays in place either way, and the choice is
  remembered.

## [0.5.2] — Shops, items, forest

- In shops and conversations (immersive mode), the message line with the choices (Yes / No...) is
  joined under the game screen, as wide as the scene, and can be clicked there.
- An item taken from the inventory can be carried out into the 3D dungeon: aiming ahead of the
  party (far away, up in the air) points into the game's view, to throw or drop it.
- The game screen hides as soon as a conversation, a shop or an encounter ends (the engine now
  says so before its fade), and right away when the map or a character sheet closes.
- Forest: puddles lie flat on the ground (they stood upright in the middle of their block), and
  always face you as in the game.
- Pictures at the end of forest paths (a house, a castle) are drawn on the ordinary forest wall,
  without black around them or green streaks down to the ground; sides where the game draws no
  picture up close are ordinary forest walls instead of black walls.

## [0.5.1] — Windows joined together

- The message line is part of the spell scroll window, right under it (no longer a window of its
  own).
- The right window holds the characters one under the other, the atlas column (lamp, purse) to
  their right, and below them a strip with the options and rest buttons.
- The windows go back to this layout once.

## [0.5.0] — One app, two views

- This is now the main project, and it includes the classic view of lol-vr (whose history and
  documentation are merged here).
- At first launch, a window asks whether you want the **classic view** or the **immersive
  mode**, and says it can be changed later from the gear on the left wrist.
- Wrist menu: "Immersive mode: on/off". In immersive mode there is only one view, the 3D dungeon
  (no mixed reality, no 486 PC; the left stick click no longer changes the scenery), and the next
  row chooses "Game screen: if needed" or "always". In the classic view, everything is as in
  lol-vr: library, mixed reality, 486 PC, and "Classic screen" for the whole image.
- Documentation (docs/wiki) for both views.

## [0.4.0] — Walking, shop doors, windows, light

- Walking follows your gaze: the left stick points relative to where you look (forward, back,
  side steps), whatever the party's direction. No more side step when you meant to go forward.
  When you stand still, the party quietly turns to face where you look (not while you aim at a
  window).
- Shop doors and passages to other regions (the pictures at the end of forest paths) are walls
  with their picture: the view stays in front of them when the game takes the party through. No
  more fake room behind a shop door, no more being inside a block of big pixels.
- The game screen hides again as soon as a shop, conversation or encounter is over.
- New window layout: the messages sit under the spell scroll, as wide as it, without overlapping
  it; the characters stand one under the other in a column with the atlas, lamp and purse, with
  the options and rest buttons under them; the movement arrows are gone (the sticks do that). The
  windows go back to this layout once.
- Forest ceiling: back to the picture of the treetops from the game, like in the first version
  (the sky did not work).
- Items hidden in a wall (a key in a trapdoor) no longer stick out of the wall; once the hiding
  place is open, they show in it, where the game shows them.
- Dark places are dark in 3D too, and the lamp lights them, following the game's own brightness.

## [0.3.1] — Shops and dialogues

- The game screen now appears in front of you when the game shows something else than the
  dungeon in its view: shops, conversations, special scenes. Before, it stayed hidden in shops.

## [0.3.0] — Monsters in 3D, clicks in the dungeon

- Monsters and items near the party appear in the 3D dungeon, drawn by the game itself and always
  facing you (like in the original game), at their real place and size, animated.
- You can click in the 3D dungeon: on the wall right in front of the party (door buttons, levers,
  keyholes, switches) and on the floor in front of you (to pick up items). The pointer glows where
  it aims.
- Doors and their frames now stand together in the middle of their block, like in the game.
- The game screen no longer pops up for monsters and items (they are in 3D now); it still comes
  for scenes, dialogues, the map, menus and character sheets.

## [0.2.0] — Comfort, doors, objects, sky

- Turning no longer rotates the dungeon smoothly around you (it made people queasy): a very short
  fade to dark, and you are facing the new way.
- You walk where you look: if you look more than 45° away from the party's direction and push the
  stick, the party first turns that way, without the dungeon moving.
- The game screen only appears when it is needed: monsters or items close ahead (they are not in
  3D yet), scenes and dialogues, the map, menus, a character sheet. It comes back where you look,
  and hides again a moment later. "3D dungeon, screen always" in the wrist menu keeps it shown.
- Smaller, closer windows and screen by default (your previous layout is reset once).
- Doors stand in the middle of their block and touch the floor, like in the game.
- Fountains, statues and other things in the middle of a room are drawn alone, without a block of
  wall; puddles, plates and the like lie on the floor.
- Outdoors (forest, swamp, Yvel), the sky replaces the ceiling.
- When your head goes into a wall, the dungeon dims instead of showing the void.
- X makes a right click where you aim: drink a potion, use an item. The keyboard opens from the
  wrist menu.

## [0.1.0] — First 3D dungeon

- The dungeon is drawn in true 3D around you, from your own game files: the walls of every level
  at real size, with the floor and ceiling rebuilt from the game's background.
- Each wall type is drawn by the game engine itself, seen face on, so banners, torches, levers,
  keyholes and doors look exactly like in the game.
- The party's position is under your feet and its direction straight ahead; moves and turns glide
  in a fraction of a second. Doors that open or close update the walls.
- "3D dungeon: on/off" in the wrist menu.
- Based on lol-vr 0.11.0: game screen, detached windows, keyboard, wrist menu, English and French.
- Not yet: monsters and items in 3D, outdoor skies.
