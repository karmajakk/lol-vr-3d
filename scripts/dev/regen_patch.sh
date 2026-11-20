#!/usr/bin/env bash
# Regenerates patches/0003 from the engine changes in $SCUMMVM (the patched tree), against the
# upstream files with 0001 and 0002 applied. 0001 and 0002 are kept as they are.
# Prints the files covered; check them with "git diff --stat patches/".
source "$(dirname "$0")/common.sh"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
cd "$SCUMMVM"
FILES=$(git status --short --untracked-files=no | awk '{print $2}' | grep '^engines/kyra\|^backends/platform/libretro/src' || true)
for f in $FILES; do
	mkdir -p "$T/$(dirname "$f")"
	git show "HEAD:$f" > "$T/$f"
done
cd "$T"
git init -q
git apply "$ROOT/patches/0001-"*.patch "$ROOT/patches/0002-"*.patch 2>/dev/null || {
	# 0001/0002 touch files that are not all in FILES: apply them one file at a time
	for p in "$ROOT/patches/0001-"*.patch "$ROOT/patches/0002-"*.patch; do
		for f in $(grep '^diff --git' "$p" | awk '{print substr($3,3)}'); do
			[ -f "$f" ] || { mkdir -p "$(dirname "$f")"; git -C "$SCUMMVM" show "HEAD:$f" > "$f"; }
		done
	done
	git apply "$ROOT/patches/0001-"*.patch "$ROOT/patches/0002-"*.patch
}
git add -A
git -c user.email=dev@local -c user.name=dev commit -qm base
for f in $FILES; do cp "$SCUMMVM/$f" "$T/$f"; done
OUT="$(ls "$ROOT/patches/0003-"*.patch)"
git diff -- engines > "$OUT"
git diff --stat -- engines | tail -1
