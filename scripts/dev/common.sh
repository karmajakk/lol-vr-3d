# lol-vr-3d dev tools: shared settings. Sourced by the other scripts, not run on its own.
#
# Environment variables (all optional except LOLVR_GAME for the game tests):
#   LOLVR_DEV   work folder (default: <repo>/build/dev)
#   SCUMMVM     ScummVM source tree with the project's patches applied
#               (default: $LOLVR_DEV/scummvm, cloned and patched on first use)
#   LOLVR_GAME  your Lands of Lore folder (the one holding GENERAL.PAK, or its parent)
#   KHRONOS     folder holding the EGL-Registry and OpenGL-Registry clones (headers for Linux tests)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEV="${LOLVR_DEV:-$ROOT/build/dev}"
SCUMMVM="${SCUMMVM:-$DEV/scummvm}"
KHRONOS="${KHRONOS:-$DEV/khronos}"
SCUMMVM_TAG="$(grep -m1 '^SCUMMVM_TAG=' "$ROOT/scripts/build.sh" | cut -d'"' -f2)"
LIBRETRO="$SCUMMVM/backends/platform/libretro"
CORE="$LIBRETRO/scummvm_libretro.so"
BIN="$DEV/bin"
mkdir -p "$DEV" "$BIN"

need_scummvm() {
	if [ ! -d "$SCUMMVM/.git" ]; then
		echo "ScummVM $SCUMMVM_TAG -> $SCUMMVM"
		git clone --depth 1 --branch "$SCUMMVM_TAG" https://github.com/scummvm/scummvm.git "$SCUMMVM"
		for p in "$ROOT"/patches/*.patch; do
			echo "patch: $(basename "$p")"
			git -C "$SCUMMVM" apply "$p"
		done
	fi
	cp "$ROOT/core/lite_engines.list" "$LIBRETRO/lite_engines.list"
}

need_khronos() {
	[ -d "$KHRONOS/EGL-Registry/api" ] || git clone --depth 1 https://github.com/KhronosGroup/EGL-Registry.git "$KHRONOS/EGL-Registry"
	[ -d "$KHRONOS/OpenGL-Registry/api" ] || git clone --depth 1 https://github.com/KhronosGroup/OpenGL-Registry.git "$KHRONOS/OpenGL-Registry"
	GL_INCLUDES="-I$KHRONOS/EGL-Registry/api -I$KHRONOS/OpenGL-Registry/api"
}

# The ScummVM system folder for the tests: settings, kyra.dat; saves in $DEV/saves.
need_sysdir() {
	SYS="$DEV/sys"
	mkdir -p "$SYS/scummvm/extra" "$DEV/saves"
	cp -u "$SCUMMVM/dists/engine-data/kyra.dat" "$SYS/scummvm/extra/"
	cat > "$SYS/scummvm.ini" <<INI
[scummvm]
savepath=$DEV/saves
extrapath=$SYS/scummvm/extra
libretro_playlist_path=$SYS
subtitles=true
INI
}

game_dir() {
	: "${LOLVR_GAME:?set LOLVR_GAME to your Lands of Lore folder}"
	local pak
	pak="$(find "$LOLVR_GAME" -maxdepth 3 -iname GENERAL.PAK | head -1)"
	[ -n "$pak" ] || { echo "GENERAL.PAK not found under $LOLVR_GAME"; exit 1; }
	# The game folder is the parent of the DATA folder when there is one (CD version), else the PAK's folder.
	local d; d="$(dirname "$pak")"
	case "$(basename "$d")" in DATA|data) dirname "$d" ;; *) echo "$d" ;; esac
}
