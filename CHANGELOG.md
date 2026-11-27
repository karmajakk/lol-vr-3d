# Changelog

Format: [semantic versioning](https://semver.org/). While the number starts with 0, the project
is in development and anything may still change.

## [0.58.0] — Cinema for places only, softer glow, dimmed Exit

- The cinema mode is for places, shops and sequences only: a character who talks over the
  dungeon view (Timothy after the inn) no longer turns the world black.
- The glow around the cinema screen is narrower and much softer (it looked like dark walls
  spilling over the windows).
- The greyed-out "Exit" button during a dialogue is drawn in a dimmer yellow instead of the
  game's checkerboard, which broke the letters into dots once enlarged.
- Forest canopies are darker, closer to the foliage of the walls (Southland).
- The mine entrance of Upper Opinwood: a wall of trees behind the picture hides the dead-end
  block behind it.

## [0.57.0] — Objects facing you, mine entrance, X for the spell level

- An object in the middle of a block (the thief in the forest, a statue...) is one picture turned
  toward the party, as in the game, instead of two crossed pictures seen from the side.
- A place picture at the end of a corridor with a dead-end block behind it (the mine entrance
  from Upper Opinwood) is at the back of its block, like the other places to visit.
- The cinema screen is lower by default (the characters' eyes about a quarter from the top, at
  eye level).
- Default controls: X (right click) chooses the spell level when the level menu is open; R3 only
  recenters.

## [0.56.0] — Cinema screen you can place

- The intro and the character selection take place in your room again (the fire, the window). When
  the game starts, it goes dark until the guard speaks, then the cinema mode takes over.
  ("Intro, menus: Dark" in the VR controls keeps them in the dark room.)
- The cinema screen can be moved (its handle) and resized (its corners, or L3 + right stick:
  up/down the size, left/right the distance). The size, the distance and the height are kept for
  the next scenes; it always appears straight ahead. Choosing a size in the VR controls puts it
  back to its original place and size.

## [0.55.0] — Cinema mode, closer and at eye level

- The cinema screen is placed in front of the head when the scene starts (or at recentering),
  closer (1.5 m) and more curved (60° medium, 72° large, 48° small), with the characters' eyes at
  eye level. It is centered on the player, whatever the picture's width.
- The game windows follow it: those beside the picture move aside, those below go down just
  enough to stay under its bottom edge (nothing overlaps the picture any more; the dialogue
  buttons, such as "Exit", stay visible). The screen no longer hides a window.
- The dark room has a faint tint of the scene's colors.
- The atlas (minimap) is hidden during scenes and shops, and comes back after.
- After the intro, the game stays dark until the first scene (the guard), without showing the
  castle first. The curved screen goes dark when there is no scene to show.

## [0.54.0] — Cinema mode

- Immersive mode: during a scene that shows its own picture (shop, place, character, cutscene),
  the dungeon fades to black in 0.4 s and the game picture appears on a curved screen around the
  player, without a frame, with a soft glow in the colors of its edges. The text, the inventory
  and the portraits stay. It fades back when the scene ends.
- Before the game (intro, menus, character selection), the same dark room, unless "Intro, menus"
  is set to "Scenery" in the VR controls.
- The size of the cinema screen is a setting (VR controls, "Cinema"): medium (55°), large (68°),
  small (44°), or off (the old flat screen). It cannot be moved or resized by hand; L3 recenters.
- The pointer aims at the curved screen.

## [0.53.0] — Saving in shops, wall pictures, bigger tabs

- Swamp: the ceiling is a copy of the swamp wall.
- Opinwood beggar, the mine exit and the barrier of Upper Opinwood: drawn as a single picture at
  the back of the block (or across a corridor), like the other places to visit, instead of a cross.
- Clicking a side wall turns the party back to its direction afterwards, so it no longer ends up
  90° off (seen after resting in the swamp).
- The inventory tabs are bigger and easier to click.
- In a shop or a place, the floppy on the wrist makes a quick save (slot 990, "VR - quick save"),
  confirmed by three pulses in the left hand. Load it from the game menu.
- The ZZZ icon is no longer black at the character selection.
- Coming back from a shop or a place in the same level no longer reloads the textures: smoother.
- "You have been defeated": the black bands above and below are trimmed.

## [0.52.0] — Ceilings as pixelated as the walls

- The drawn ceilings (coffers, mine planks, rock, treetops) now have pixels as big as the walls'
  (two texels per game pixel) and a light grain, so they no longer look finer than the walls.
  The cave ceilings, copies of their walls, are unchanged.
- Opinwood (a Forest region like the others) gets the same treetops in its walls' greens.

## [0.51.0] — Forest canopy in the walls' greens

- Outdoors (Northland and Southland forests, swamp), the leaves of the ceiling take the greens of
  the trees on the walls: blue-green in one forest, plain green in the other.

## [0.50.0] — Cave and castle ceilings

- Caves: the ceiling is the cave wall itself.
- Gladstone castle: the coffers take the pale tone of the castle walls.

## [0.49.0] — Real ceilings, smoother arrivals

- **Ceilings** drawn per region, in the colours of the game's own ceiling: coffers in Gladstone
  castle, Roland's manor and the towers; wooden planks and beams in the mines; textured rock in
  the caves and ruins; treetops with sky between them in the forest, the swamp and Yvel.
- Arriving in a new region (leaving the castle, the first meeting with the guard) no longer shows
  the walls one way for a split second and then another: the room waits, dark, for the walls as
  the game draws them, then fades in.
- The ZZZ button without the black lines around it.
- Game menus shown on their own start at the default screen size (not the size of a screen
  enlarged for cutscenes); a menu size saved by 0.48 is dropped.

## [0.48.0] — Wrist buttons back, menus sized on their own

- The floppy and ZZZ are buttons again, with their background (the ZZZ is the game's own rest
  button), only without the dark frame around them.
- A game menu shown on its own (options, save, quit, defeat) has its own size: making the screen
  bigger for cutscenes no longer makes the menus huge, and resizing while a menu is shown sizes
  the menus only.
- Grouped interface: no more backing panel behind the block (its windows simply touch); with a very
  large block, it could be drawn over the windows depending on the viewing angle.

## [0.47.0] — Fixes after a rest, smaller tabs and corners

- After resting, the dungeon could end up a quarter turn off from the game (walking "forward"
  went into a wall) until the game was reloaded: two turns close together (the ones queued while
  resting) are now taken in order, and the room always follows the game's direction.
- The floppy and ZZZ are just the icons, without frame or background, a little smaller and out of
  the wrist's brass band. The ZZZ is taken from the game's own rest button and only shows when the
  game has one (not during the introduction). The floppy is no longer upside down.
- The "quit the game?" box (like the defeat box) is shown without the black around it.
- Inventory tabs: one slot wide, above the first and last slots; their texture comes from an empty
  slot only (an oil flask could show on them), and they no longer show through the keyboard.
- The game's text box laid over the characters reaches the bottom of the portraits.
- The resize corners (and their grab zones) shrink with small windows.

## [0.46.0] — Watches on the side of the wrist, game menus kept apart

- The floppy and ZZZ buttons sit on the side of the wrist (thumb side), like a watch you look at
  from the side, turned a quarter to read upright.
- The inventory grows in place when opened, in the plane of the other windows (it only moves
  forward when it would go behind the screen).
- The inventory tabs use the game's own inventory slot texture and colour.
- Game menus and the "You have been defeated" box no longer show through the interface windows,
  which keep the image from before; the defeat box is shown without its black bands.

## [0.45.0] — Grouped or separate windows, sober edges

- **LOL2 UI removed**: it did not fit the game.
- "Interface" (immersive mode) now offers **Grouped** (the big block, as before), **Windows**
  (separate windows again: the spellbook, the characters with the message line above them, the
  inventory with its tabs above it and the compass column, each with its own handle, placed and
  resized on its own; the inventory opens upwards) and **Classic** (the game's bottom bar).
- Grouped: the resize corners are only the four corners of the big frame.
- Every window now has a thin dark edge, like the lines around the game's own interface areas,
  instead of the wide gold frame.

## [0.44.0] — The block reworked, book tabs

- **Windows interface**: the inventory now sits on top of the block and the message line with the
  characters at the bottom; the spellbook and the compass column run the full height on either
  side. The whole block has one brass frame (thin seams between its windows), so corners no
  longer overlap, and it grows or shrinks by the corners of that frame.
- The inventory opens **upwards**, in front of the view; its tabs sit above it.
- **Book tabs** as in Lands of Lore 2, in the inventory's colour with a brass edge and rounded top.
  The open/close tab lights the arrow pointing outwards when the inventory is closed and the one
  pointing inwards when it is open.
- The yellow frame of the selected character no longer flashes over the game's text box while it
  slides down over the portraits.
- LOL2 UI uses the same single frame and tabs.

## [0.43.0] — LOL2 UI

- New interface choice in "VR controls" → "Interface": **LOL2 UI**, one tall block left of the
  view, as in Lands of Lore 2. From the bottom up: the message line; the inventory standing as a
  column (one quick row of 8 slots), the characters stacked one above the other and the compass
  column; the spellbook at the top. "Interface" now cycles Windows → Classic → LOL2 UI.
- LOL2 UI inventory tabs, left of the column: two opposed arrows at the top open the whole
  inventory towards the left (another click closes it); three bars at the bottom sort it (rows 2
  to 6; the quick row is never sorted).
- When the game's text box grows, it lies over the bottom of the block, on the portraits.
- The atlas sits to the right of the view in LOL2 UI.

## [0.42.0] — One big block, Lands of Lore 2 tabs

- **Windows interface**: one big block below the view, like the bottom of the game's screen:
  spellbook on the left, message line + characters + inventory in the middle, compass column on
  the right. It moves and resizes as one piece (one handle, under the inventory). Detaching
  windows is gone for now.
- The inventory shows **two quick rows** (16 slots), never sorted; sorting orders rows 3 to 6.
- **Tabs** under the inventory, as in Lands of Lore 2: bottom right, two opposed arrows open and
  close the whole inventory; bottom left, three bars sort it (works closed too).
- The floppy and ZZZ buttons are on the **inner side of the wrists**, upright when you look at
  the inside of your wrist.
- The yellow frame of the selected character no longer shows over the game's text box while it
  opens over the portraits.
- Windows placed with an earlier version are laid out again once.

## [0.41.1] — Licenses

- Third-party components and their licenses listed in `THIRD_PARTY_NOTICES.md`; the license
  texts (project, ScummVM, OpenXR loader, Android NDK glue, DejaVu font) are packaged in the APK
  under `assets/licenses/`.
- Credits: ScummVM only (no code or data from other projects is used; the format notes remain
  cited in the README).

## [0.41.0] — Wrist watches

- The floppy and the rest button (ZZZ) are now **watches on the wrists**, always there: left
  watch, the game menu; right watch, rest. Look at it, click it with the other hand.
- The inventory is as wide as the characters' block (larger slots); its buttons are fully
  outside it, on the right, and their icons show again (they were blank).
- The chain buttons (detach the inventory, detach the compass column) appear only while the
  finger is on them, and disappear as soon as it leaves.

## [0.40.0] — Buttons outside the windows

- The inventory window holds only its slots; its buttons sit just outside, on its right, under
  the characters: unfold/fold, sort (unfolded) and, only when you point at the inventory, the
  chain that detaches it (right of the others). The compass column's chain button appears
  under the column, only when you point at it.
- The game's enlarged text box stays within the characters' block (over the portraits), no
  longer spilling onto the inventory.
- The message line alone (arriving in front of a place, e.g. Lake Dread) is no longer laid
  over the characters: the block's own message line shows it, always up to date.

## [0.39.0] — Attached and detachable windows

- Conversations: the game's text box now lands on the characters' block, where the game draws it
  (over the portraits), instead of floating in front of or behind it (two texts on top of each
  other, e.g. the guard's welcome, Scotia).
- The inventory is **attached** right under the characters (same plane, same scale, one frame);
  resizing it resizes the whole block. A chain button (shown when you point at the inventory)
  **detaches** it; putting it back against the bottom of the characters attaches it again.
- Same for the **compass column**: a chain button on it (between the lamp and the purse) detaches
  it from the spellbook into a window of its own; put it back against the spellbook's right edge
  to attach it again.
- The rest button (ZZZ) is on the **back** of the right hand, like the floppy on the left one.

## [0.38.0] — Like the original game's bottom bar

- **Windows interface**: the message line and the characters **side by side** (as in the game: one
  in the middle, two or three spread out evenly) form one block with the inventory right under
  it, below the view; the block moves as one piece.
- The compass column (compass, atlas, lamp, purse) is joined to the right of the spellbook.
- The rest button (**ZZZ**) is on the **right palm**: turn it towards you, click it with the left
  hand.
- The beggar of the forest lies on the ground in the 3D dungeon, his picture's edges blended in,
  no longer a dark floating rectangle.
- Windows placed with an earlier version are laid out again once.

## [0.37.0] — Grips change character

- The **grips** change character: left grip the one above (previous), right grip the one below
  (next). The right stick up/down no longer does it (it went off by itself in the middle of a
  fight). Right click (drink, use an item) is on X. In the button editor, the grips can still be
  given another action.

## [0.36.0] — Left grip for the next character

- The left grip selects the next character (when the right hand aims).

## [0.35.0] — Classic interface in immersive mode

- **Classic interface** for the immersive mode (VR controls, "Interface": Windows or Classic): the
  game's own bottom bar under the view, as in the original (messages, movement arrows, floppy and
  rest, the characters, the item strip with its arrows), the compass column split in two at its
  right (compass and atlas, lamp and purse). No inventory window in this interface.
- The wrist floppy only appears when you turn your left hand to look at its back (like reading a
  watch), no longer when the hand rests in your lap while seated.
- Items lying on the floor are picked up from your block **or the one in front**, straight into the
  hand when you aim at them (a more generous aim, no need to point at the floor), no longer by
  looking for the game's pixels.
- Dark places without oil in the lantern: a dim light as in the game (you still see the block in
  front), no longer almost pitch black.

## [0.34.0] — Fixes from the caves

- Items lying at your feet (the party's own block) can be picked by clicking on them.
- Pressure plates are smaller and flatter, darker like in the game; forest puddles are flat again.
- Greyed-out buttons are back to the game's own look (the darkening broke colors in shops and inns).
- The arrows of the atlas window browse the other levels' maps right in the window: the game's
  atlas shows inside it (its arrows and EXIT work there), no big screen.
- Immersive mode: the party no longer turns by itself toward where you look, and the left stick
  moves relative to the party's facing, not your head.

## [0.33.0] — Pressure plates

- **Pressure plates** are round slabs in relief: an engraved circle, a lighter bevelled edge and a
  dark groove around them; a pressed plate sits almost flush with the floor.

## [0.32.0] — Atlas pages, menu alone

- The arrows of the atlas window work: they open the game's own atlas on the screen, one page
  turned (the maps of the other levels); its arrows and EXIT then work as in the game.
- Immersive mode: the game menu (the floppy) shows alone, without the rest of the image or the frame.
- The game's pale blue frame around a selected portrait is gone (the yellow frame shows the
  character of the A and B buttons).

## [0.31.0] — Typing with your fingers

- The virtual keyboard can be typed on directly: touch a key with the tip of either index finger
  (both hands work), a short vibration confirms it. Aiming with the trigger still works.

## [0.30.1] — Doors in their recess

- Doors at the back of an opening whose frame is painted on the wall (Caves Level 1 and many
  more): the door and its frame now stand together at the back of a recess, both on the floor,
  the sides of the recess drawn from the frame (as already done for Gladstone and Roland's manor).

## [0.30.0] — Lantern

- **Lantern** (immersive mode): in dark places, how far you see follows the lantern's oil, with a
  slight flicker; with the lantern out you see almost nothing beyond your own block.
- **L3** also picks the next spell level when the levels are open; **R3** only recenters
  (immersive mode).
- Hit monsters show the game's blood splash in the 3D dungeon (blows, thrown daggers and stars,
  spells).

## [0.29.0] — Caves QA

- **L3** (immersive mode): next spell of the spellbook.
- **R3** (immersive mode) also puts your eyes at the party's height, seated or standing; a short
  vibration confirms it.
- Right stick down picks the next character in the order of the party window.
- Greyed-out buttons (EXIT during a conversation, spell levels) are darkened instead of dotted.
- The spellbook strip is gone; during a wide scene (an inn), the spellbook keeps its scroll.
- **Pits** are real holes: a shaft going down into the dark.
- A script animation outside a conversation (a wall crumbling under a hammer) shows the game's view.

## [0.28.0] — Windows held to move

- **Moving windows**: hold the trigger anywhere on a window or on the screen for a moment and it
  follows your hand; a short press is a click (sent on release). The brass bars are gone.
- The spellbook no longer folds; its title strip is thinner.
- The item in hand is drawn as the pointer over the inventory window too.
- Items bought or given in a shop or a conversation can be put in the inventory window right away.
- In front of a place's door (the Southland inn, Buck's hut), the game's view of the place shows up.
- After leaving a place, the view follows the party at once (no more short freeze).

## [0.27.1] — Fixes

- Leaving a place no longer freezes the view for two seconds: the view follows at the next step.
- The characters' portraits stay visible during scenes and conversations.

## [0.27.0] — New inventory

- **Inventory** (3D dungeon): the 48 slots of the game are now a fixed **quick bar of 8 slots**; the
  arrow button on its right unfolds the other 40 slots (5 more rows). The sort button groups those
  40 by kind (the quick bar is not sorted). Click an item to pick it up, click a slot to drop it or swap.
- The castle fountain is back to its size; the cave alcove keys sit deeper in the alcove; monsters
  that start a scene face you squarely.

## [0.26.0] — Atlas icon, scenes

- **Atlas**: the game's atlas icon opens the movable atlas window; "Close" on the page closes it.
  The brown tab and the "Atlas" VR setting are gone.
- Thieves and guards turn toward the party before the scene starts.
- Thug's Hideout exit is one flat picture; the Southland inn screen shows the door and EXIT again;
  the place's backdrop stays during inn/Marina scenes; the inventory stays visible during text boxes.
- Items on the floor can be picked by clicking them directly; alcove keys sit closer to the wall.

## [0.25.0] — Controller profiles, legal notice

- **Controller profiles**: "Controller mapping" (VR controls) now shows the buttons of the profile
  in use, and **Customize** opens a button editor. A, B, X, Y, the grip and both stick clicks can
  each do any of: attack, magic, right click, map, recenter, view, Enter, Escape, atlas window,
  next character, keyboard, or nothing. Save a profile under a name (typed on the keyboard), pick it
  again later, or delete it. **Classic** (the default) is always there and cannot be changed or
  deleted. The trigger, the sticks, the menu button and the floppy stay fixed.
- **Legal notice**: under "Credits & Requirements", on the first-launch window, at the bottom of
  VR controls, in the READMEs, the wiki and the release notes: an unofficial, non-commercial fan
  project, not affiliated with or endorsed by Electronic Arts, Westwood Studios or Virgin
  Interactive; no game files are included, a legally obtained copy is needed.

## [0.24.0] — Credits & Requirements

- "VR controls" ends with "Made possible by ScummVM & KForestland", and has a new
  **Credits & Requirements** button: a short note saying the mod runs on ScummVM and was made
  possible thanks to the hard work of KForestland (lands-of-lore-1-re). The same note is on the
  first-launch window, which is a little taller for it.

## [0.23.0] — The game's own atlas, straight to the title screen, thanks

- The Atlas window now shows the game's own atlas page — the same parchment, colors, symbols, font
  and legend — drawn again by the engine at every step (patch 0003), with your arrow.
- The ScummVM logo no longer shows at launch: the game opens on its title screen.
- A thank-you to the ScummVM team and to KForestland
  ([lands-of-lore-1-re](https://github.com/KForestland/lands-of-lore-1-re)) under "VR controls",
  on the first-launch window and in the README.

## [0.22.0] — Live atlas window

- New **Atlas** window, to the right of the party once the party has the map: the map of the level
  as you explore it, updated at every step — your arrow, the walls, doors in red, and the named
  places of the game's atlas (a colored diamond on the map, the same before its name in the legend
  underneath). No more opening the atlas to see where you are.
- Its strip ("Atlas +/-") folds it like the spellbook; it has its own grab bar under it and can be
  moved and resized like the other windows. A layout saved before this version keeps its windows:
  the atlas takes its place beside the party.
- "Atlas" On/Off in VR controls hides or shows it (remembered).
- Patch 0003 publishes the explored blocks, the map classes of the walls, the level's map title
  and its legend. Dev: `atlas NAME [replie]` in the bench writes the window as the headset draws it.

## [0.21.0] — Whole-game check of every fix, wide cutscenes, item cursor in classic view

Every fix since 0.1 was checked again on levels 3 to 24 (doors closed, half open and open; empty
views from every block; every wall seen from the block in front, clicks compared with the game's
own picture; every entrance seen from inside). What it found, now fixed:

- Walls the game never draws are no longer drawn: passages through the Southland forest that looked
  like trees, a room behind an invisible barrier (level 12), the edges of some levels (7, 8, 11,
  14). You see what the game shows, and you can click what lies beyond.
- Houses of Yvel and other entrances you walk into: when you come out, you stand in the doorway
  and see the street, instead of an empty view.
- The mine cart is an object in the tunnel, as in the game, not a picture stretched over the wall.
- Clicks land on what you see on every other wall checked (bushes, holes, levers, buttons, doors).

Also:

- Classic view: an item you pick up shows on the pointer again.
- Cutscenes wider than the view (Scotia poisoning King Richard, Baccata's energy cage): the screen
  widens to the whole scene, with all its characters and its exit button (patch 0003).
- When the party learns a spell, the spellbook window opens by itself. During a conversation, the
  spellbook keeps showing what the game draws (a spell given then is written in it), and the
  windows no longer stay black after a scene that starts with a fade.
- The grab bars of the game's windows (party, inventory, spellbook) are always under the window.
- Dev: the audit also checks clicks and entrances; the bench's party takes no damage
  (`LOLVR_BOUCLIER=0` to take it), `sort N` teaches a spell with the game's animation.

## [0.20.0] — Doors on the floor, standing in the doorway, places in full from the start

- Doors set back in their frame (Roland's manor): the door now stands on the floor. Each column of
  the frame and of the door is laid down to the floor by its own foot; before, the door stopped
  above the threshold and you could see the floor of the next room under it.
- Ways out painted in a wall (the exit of Roland's manor) and the shop doors of the castle: the
  bottom of the painted path or door touches the floor, instead of a strip of floor rising behind it.
- Leaving the manor, Buck's hut or the castle: you now stand where the game puts you, in the
  doorway itself, shown open as the game draws it. Walking on works at once, and clicks on the
  walls around (a bush, a hole in a tree) reach the game: before, the view stood one block away
  from the party, so the first steps seemed stuck and those clicks went to the wrong walls.
- Pictures of places in full from the start: the keep and the marina (level 2), Buck's hut, the
  Grey Eagle inn, Roland's manor and the second marina (Southland forest), and the king's palace,
  the hut, the fletcher and the smithy of the swamp. Others still show in full after a first visit.
- Remarks and monsters that talk (the sconces, the big orc): only the text box appears; the game's
  screen stays hidden, since its window only showed the dungeon the 3D view already shows. Scenes
  with their own picture (a shop, a person, a cutscene) still show it (patch 0003).

## [0.19.0] — Places in full size, leaving Buck's hut, the manor's exit, clicks on pictures

- Pictures of places on the walls (the marina, Gladstone keep, Buck's hut, ...): the 3D dungeon now
  shows the large picture the game opens when you walk into the place, instead of the small blurred
  one. Gladstone keep and the marina on level 2 are there from the start; the other places show
  in full once you have been in them (the engine remembers them in the save folder, for every
  game: patch 0003).
- Clicks on those pictures (bushes, holes, places) land where the game draws the small picture:
  stretched over the whole wall, a click on its edge used to fall outside it and the game ignored
  it.
- Leaving Buck's hut (and any place the game leaves you inside of after its scene): the view is
  in the level, in front of the way out, instead of inside the hut's picture with forest all
  around.
- The exit of Roland's manor: under the painted path, the floor of the doorway instead of a big
  brown smear (walls with an opening down to the floor show the floor beyond it, as in the game).
- Doors set back in their frame: the sides of the recess now stand on the floor all the way (their
  foot follows the game's drawing column by column).
- Scenes that take over the screen (meetings, cutscenes): in immersive mode they stay in the frame
  of the view, with their text under it and the party window beside it, portraits kept, instead of
  the whole game screen.
- Dev: `LOLVR_CAM` moves the bench's camera, `LOLVR_DUMP_TYPE=tous` lists every wall type of the
  level with the engine's raw drawing.

## [0.18.0] — The guard's welcome in 3D, scenes wait for the turn, messages letter for letter

- New game: the guard's welcome at the castle gate is already in immersive mode, the castle in 3D
  around his picture. The engine now hands over the party's place and the walls as soon as the
  level is loaded, before its first scene (patch 0003).
- When the game turns or moves the party and a scene follows (meeting someone, a cutscene), the
  scene now waits a moment: the party turns in the 3D dungeon first, then the scene starts. Before,
  the scene came first and the party turned only once it was over (patch 0003).
- The message line in the left window (under the spellbook) is now copied pixel for pixel. It was
  squeezed by a few pixels and lost one column in twenty, which cut pieces out of letters and read
  like typos (resting, remarks).
- Dev: `etat` in the end-to-end bench follows the game state over time.

## [0.17.0] — Whole-game check: doors, mines, the looping forest

Every level was checked on the PC (a new tool renders each door series closed, half open and
open next to the game's own picture, and looks for empty views from every block):

- Mines: no more holes in the tunnels. Blocks with rails counted as walls, so they had no floor
  and no ceiling; the rails now lie on the floor, turned the way the game draws them.
- The forest that loops on itself: walking off one side of the map leads on to the next row, as in
  the game. The 3D dungeon now shows what lies past the edge instead of an empty view, and the
  party walks across smoothly.
- Doors set back in their frame: the sides of the recess, between the face and the door, are
  now the game's own drawing (posts, carved stone, rock) instead of the plain wall next to it.
  The mines' opening buttons, drawn on the post beside the door, now show and can be clicked.
- The doors of the white marble levels (18 to 20) and of level 27 now sit back in their frame,
  between their pillars, like the castle and manor doors, instead of standing flush. Their frames
  have a one-piece lintel over the door, which the earlier rule did not recognise.
- The other doors (castle, manor, ruins, caves, volcano) were checked against the game and keep
  their look.

## [0.16.0] — Spell levels on the buttons, manor doors flush, text box apart

- Magic with the controllers: **B** opens the levels of the chosen spell with level 1 framed in the
  party window, the **right stick click** frames the next level the character can cast (1 to 4,
  then back to 1), and **B** again casts the framed level. The engine now reports the open spell
  menu and how many levels the character can afford (patch 0003). The right stick click still
  recenters when the menu is closed.
- Roland's manor doors: the door and its pillars are again one plane, flush, as in the game; the
  pillars and the threshold reach the floor, open or closed, and the door rises between them.
- While the game's text box covers the party's portraits (a remark, a conversation), the party and
  spellbook windows keep showing them as they were: the box has its own window under the screen.

## [0.15.0] — Roland's manor: doors, alcoves, remarks

- Saves made in Roland's manor no longer crash the engine when they load: some wall types of the
  level's table point past its graphics, and the wall capture now leaves them out (patch 0003).
- Roland's manor doors sit back between their pillars, as in the game: they no longer cover the
  pillars and the button that opens them. (Castle doors, whose frame has pillars at the door's
  height, keep their set-back frame.)
- Items in an alcove sit on its shelf, at the height the game shows them, instead of at the foot
  of the wall.
- Text is on by default (voice and text): a character's remark, with the portrait, shows in the
  text window under the screen. Before, the CD version only spoke, so the window stayed empty.
  The game's options can still turn the text off.
- The inventory row's grab bar is under the row, not at its side.

## [0.14.0] — Corridor ends, text window, cursor, side walls

- Corridors that reach the edge of the map no longer end on black: like the game, the wall
  across the edge (the one on the other side of the map) closes them.
- Clicking a bush, nest or hole on a side wall now turns the party toward it first, then clicks:
  the game only reacts to the wall in front.
- The message line and the conversation box are now their own window just under the frame of the
  game screen, instead of inside it. A character's remark (the portrait and text in the expanded
  field) is shown there too: the engine now reports when the text field is expanded (patch 0003).
- Small windows (the message line under the spellbook) no longer drop thin letters (l, r, i):
  game pixels smaller than a headset pixel are averaged instead of sampled.
- The game's mouse cursor is drawn by the app, only on the screen or window you point at; it no
  longer spills onto a neighbouring window (patch 0003: the core can leave the cursor out).
- Castle doors: the wall above the doorway (lintel) is drawn at the face, open or closed.
- Pictures of another region or house at the end of a path now cover the whole wall.
- Arriving in a region on its entrance (leaving Castle Gladstone, for example) puts the view in
  front of the entrance, not inside the wall.
- Dev: the end-to-end bench can teleport (`goto LEVEL BLOCK DIR`), print the map (`carte`) and
  look for empty (black) views (`LOLVR_NOIR`); test games use subtitles.

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
