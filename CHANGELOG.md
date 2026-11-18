# Changelog

Format: [semantic versioning](https://semver.org/). While the number starts with 0, the project
is in development and anything may still change.

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
