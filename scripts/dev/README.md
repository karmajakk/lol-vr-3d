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
`carte` (print the map: `#` solid, `.` open, `@` the party).

`LOLVR_DUMP_TYPE=portes` lists every door type (depth, plane, how far raised) and writes their
pictures; `LOLVR_FORCE_DOOR=T` shows the closed doors (type 3) as type T, to see each step;
`LOLVR_NOIR=1` makes `dungeon`/`dungeonat` report views with empty (black) areas instead of
writing pictures.

The test save is the start of the game in English with one champion, Michael, in the throne room
(slot 0). Don't save into slot 0 in your actions; `E2E_NEW_SAVE=1` remakes it.

## Regenerating patch 0003

Patches 0001 and 0002 are kept as they are; 0003 holds every later engine change.
`scripts/dev/regen_patch.sh` rebuilds it from the patched tree.
