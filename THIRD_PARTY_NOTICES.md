# Third-party notices

The app's own code (`app/src`, `scripts`, `android`, `core`) is this project's work, released
under GPL-3.0 (see `COPYING`). The APK also contains or is built with the components below. Their
license texts are in this repository and, in the APK, under `assets/licenses/`.

| Component | What it does here | License | Text |
|---|---|---|---|
| [ScummVM](https://www.scummvm.org/) (built from source, with the patches in `patches/`) | Runs the game (Kyra engine) as a libretro core | GPL-3.0-or-later | `COPYING`; ScummVM's own `COPYRIGHT`, `AUTHORS` and `LICENSES/` are packaged in the APK |
| libretro API header (`libretro.h`), the RetroArch team | Interface between the app and the core | MIT | in the header |
| [Khronos OpenXR loader for Android](https://github.com/KhronosGroup/OpenXR-SDK) | Talks to the headset | Apache-2.0 | `LICENSES/Apache-2.0.txt` |
| `android_native_app_glue` (Android NDK) | Native Android app entry point | Apache-2.0 | `LICENSES/Apache-2.0.txt` |
| [stb_image](https://github.com/nothings/stb) (`app/src/third_party/stb_image.h`), Sean Barrett | Loads pictures | MIT or public domain | in the header |
| [DejaVu Sans Mono Bold](https://dejavu-fonts.github.io/) (bitmap made by `scripts/gen_font.py` into `app/src/font8x16.h`) | Text of the in-headset windows | Bitstream Vera license (DejaVu changes public domain) | `LICENSES/DejaVu-Fonts.txt` |

Source code: this repository, and ScummVM at the tag given in `scripts/build.sh` with the patches
in `patches/`.

No game files are included: *Lands of Lore: The Throne of Chaos* is © its respective owners and
must be supplied by the player.
