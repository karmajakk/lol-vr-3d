# Dev tools: testing on Linux without the headset

These scripts build the ScummVM core and the test benches for a Linux PC, so the game and the
app's 3D scene can be checked without a Quest. They work in `build/dev/` (or `$LOLVR_DEV`).

| Script | What it does |
|---|---|
| `build_core.sh [--kyra]` | Builds the core with the project's settings (Lands of Lore only, no OpenGL) and checks it. `--kyra` first deletes the engine's objects: needed after changing an engine header. |
| `build_tests.sh` | Builds `scene_test` (the app's scene and interface) and `e2e_test` (the real game through the core). |
| `e2e.sh <out> "<actions>"` | Plays a list of actions in the real game and writes screenshots. |
| `e2e_save.sh <out> "<actions>"` | Same, starting from a test save (made on first use). About 25 s instead of a minute or more. |
| `regen_patch.sh` | Regenerates `patches/0003` from the engine changes in `$SCUMMVM`. |
| `audit.py <out> [LEVEL ...]` | Checks every level: each door series closed, half open and open, then as it is in the game next to the game's own picture (`<out>/niveauN/portes_niveauN.png`), and every open block in every direction for empty views. About 3 minutes a level; `AUDIT_DOORS_ONLY=1` skips the views. |

Environment: `LOLVR_GAME` = your Lands of Lore folder (needed for the game tests). Optional:
`SCUMMVM` (a ScummVM tree with the patches applied; otherwise cloned and patched in `build/dev/`),
`LOLVR_DEV`, `KHRONOS`.

**Never run a bare `make` in ScummVM's libretro folder.** Without `LITE=1 FORCE_OPENGLNONE=1` it
builds every ScummVM engine and mixes settings into the objects, which then no longer link.
`build_core.sh` always uses the right settings.

## Examples

```sh
export LOLVR_GAME="$HOME/Games/Lands of Lore"
scripts/dev/build_core.sh && scripts/dev/build_tests.sh
build/dev/bin/scene_test /tmp/scene uitest          # interface checks, prints TOUT EST BON
scripts/dev/e2e_save.sh /tmp/o "click 0.3625 0.95; wait 1; shot potion"
```

Actions (separated by `;`): `wait S`, `key NAME` (esc, enter, up, down, left, right, space, or a
letter), `click U V [S]` and `rclick U V [S]` (fractions of the 320 x 200 picture; S = wait after the
release, 0.3 s by default), `lang N`, `shot NAME` (picture, game state, and the game's cursor when
it changes), `dungeon NAME` (the 3D dungeon next to the game picture), `watch S` (objects in
flight, step by step, for S seconds), `goto LEVEL BLOCK DIR` (move the party there),
`carte` (print the map: `#` solid, `.` open, `@` the party), `atlas NAME [replie]` (the atlas window as the headset draws it, NAME.ppm), `dungeonat NAME BLOCK DIR [B FACE TYPE]`
(the 3D dungeon seen from any block, optionally with one wall face set to another type),
`sort N` (the party learns spell N, with the game's animation), `etat SECONDS [NAME]` (every change of the game state: view, scene, text box, block, direction;
with NAME, a picture at each change).

`LOLVR_DUMP_TYPE=portes` lists every door type (depth, plane, how far raised) and writes their
pictures; `LOLVR_FORCE_DOOR=T` shows the closed doors (type 3) as type T, to see each step;
`LOLVR_CAM=x,y,z,yaw,pitch` moves the camera of `dungeon`/`dungeonat` inside the block (metres,
degrees); `LOLVR_DUMP_TYPE=tous` (or a type number, or `portes`) lists the wall types with their
pictures (`typeN_mur.ppm` as used in 3D, `typeN_brut.ppm` as the engine drew it);
`LOLVR_CLIC=1` checks clicks: rays through the 3D picture, the point of the game's screen the app
would click, its colour compared with the 3D view (and with a point 8 pixels aside, as a witness).
The bench's party takes no damage unless `LOLVR_BOUCLIER=0`.
`LOLVR_MURS=1` makes `carte` also list the wall types of every block (north, east, south, west);
`LOLVR_NOIR=1` makes `dungeon`/`dungeonat` report views with empty (black) areas instead of
writing pictures.

The test save is the start of the game in English with one champion, Michael, in the throne room
(slot 0). Don't save into slot 0 in your actions; `E2E_NEW_SAVE=1` remakes it.

## Regenerating patch 0003

Patches 0001 and 0002 are kept as they are; 0003 holds every later engine change.
`scripts/dev/regen_patch.sh` rebuilds it from the patched tree.
