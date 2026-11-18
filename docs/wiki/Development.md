# Development

## Code layout

- `app/`: the Quest app (C++, OpenXR, OpenGL ES). `core_host.cpp` runs ScummVM; `main_xr.cpp` handles the headset, controllers, layers and haptics.
- `app/src/scene.cpp`, `room.cpp`, `props.cpp`, `proctex.cpp`, `ambience.cpp`, `ui.cpp`: rendering, room and props, generated textures, spatial sound, windows, keyboard and wrist menu.
- `app/src/dungeon.cpp`, `dungeon_data.cpp`: the immersive mode's 3D dungeon (textures from the engine's own drawings, mesh, sprites).
- `app/test/`: test benches that run on a PC without a headset.
- `core/` and `patches/`: ScummVM settings and patches (libretro core limited to Lands of Lore). The first patch adds the absolute VR pointer; the second tells the app the game area, the screen in use (character sheet, menus) and events (hits, sounds); the third publishes the level, the engine's drawings of walls, doors, monsters and items, and the light for the 3D dungeon.
- `scripts/build.sh`: builds the APK.
- `keystore/debug.keystore`: a public test signing key, only used so updates install over the previous version.

Code comments are in French.

## Building and versions

Every push is built automatically by GitHub Actions (`.github/workflows/build.yml`).

To **publish a version**:

1. Add a `## [X.Y.Z]` section at the top of `CHANGELOG.md`.
2. Put the same number in the `VERSION` file.
3. Push to the `main` branch: the build publishes version `vX.Y.Z` with the APK and the changelog notes.

Numbering (semantic versioning): the first digit changes for a major milestone, the second for a new feature, the third for a fix. While the number starts with 0, the project is in development.

## License

GPL version 3 or later, like ScummVM (see `COPYING`). *Lands of Lore* is a trademark of its respective owners; this project is not affiliated with Westwood, Electronic Arts or the ScummVM team.
