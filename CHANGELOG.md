# Changelog

Format: [semantic versioning](https://semver.org/). While the number starts with 0, the project
is in development and anything may still change.

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
