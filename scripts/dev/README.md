# Dev tools: testing on Linux without the headset

These scripts build the ScummVM core and the test benches for a Linux PC, so the game and the
app's 3D scene can be checked without a Quest. They work in `build/dev/` (or `$LOLVR_DEV`).

| Script | What it does |
|---|---|
| `build_core.sh [--kyra]` | Builds the core with the project's settings (Lands of Lore only, no OpenGL) and checks it. `--kyra` first deletes the engine's objects: needed after changing an engine header. |
| `build_tests.sh` | Builds `scene_test` (the app's scene and interface) and `e2e_test` (the real game through the core). |
| `e2e.sh <out> "<actions>"` | Plays a list of actions in the real game and writes screenshots. |
| `e2e_save.sh <out> "<actions>"` | Same, starting from a test save (made on first use). About 25 s instead of a minute or more. |

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
letter), `click U V` and `rclick U V` (fractions of the 320 x 200 picture), `lang N`, `shot NAME`
(picture, game state, and the game's cursor when it changes), `dungeon NAME` (the 3D dungeon next
to the game picture).

The test save is the start of the game in English with one champion, Michael, in the throne room
(slot 0). Don't save into slot 0 in your actions; `E2E_NEW_SAVE=1` remakes it.

## Regenerating patch 0003

Patches 0001 and 0002 are kept as they are; 0003 holds every later engine change. To regenerate
it, diff the changed files of the patched tree against the upstream tag with 0001 and 0002
applied (in a scratch git repository), and replace `patches/0003-niveau-pour-le-donjon-3d.patch`.
