#!/usr/bin/env bash
# Builds the ScummVM core for Linux, for the tests, ALWAYS with the project's settings:
# Lands of Lore engine only (LITE=1) and no OpenGL (FORCE_OPENGLNONE=1).
# Never run a bare "make" in the libretro folder: it builds every ScummVM engine and mixes
# settings into the objects, which then no longer link.
# Usage: build_core.sh [--kyra]
#   --kyra  deletes the kyra engine objects first. Needed after changing an engine header:
#           the build does not track header dependencies (a changed struct size corrupts memory).
source "$(dirname "$0")/common.sh"
need_scummvm
cd "$LIBRETRO"
if [ "${1:-}" = "--kyra" ]; then
	find . -path '*engines/kyra*' -name '*.o' -delete
	echo "kyra objects deleted"
fi
LOG="$DEV/core_build.log"
make LITE=1 FORCE_OPENGLNONE=1 -j"$(nproc)" > "$LOG" 2>&1 || { grep -n " error\|Error " "$LOG" | head -20; echo "build failed, see $LOG"; exit 1; }
SYMS="$(nm -D "$CORE")"
for s in retro_lolvr_state retro_lolvr_level retro_lolvr_set_lang retro_lolvr_inventory; do
	grep -q " T $s\$" <<< "$SYMS" || { echo "FAILED: $s not exported (patches missing?)"; exit 1; }
done
if grep -q "OpenGL7Context" <<< "$SYMS"; then
	echo "FAILED: OpenGL in the core (wrong settings). Delete the objects built since then and rerun."
	exit 1
fi
echo "core ready: $CORE"
