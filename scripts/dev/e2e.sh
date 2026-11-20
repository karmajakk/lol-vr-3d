#!/usr/bin/env bash
# Runs the real game through the core and plays a list of actions (see app/test/e2e_test.cpp).
# Usage: e2e.sh <out folder> "<actions>"      e.g. e2e.sh /tmp/o "wait 8; key esc; wait 3; shot menu"
# With E2E_FROM_SAVE=1 (or through e2e_save.sh) the actions start in the test save.
source "$(dirname "$0")/common.sh"
OUT="$1"; ACTIONS="$2"
mkdir -p "$OUT"
need_sysdir
GAME="$(game_dir)"
# Loading slot 0 from the main menu ("Load a game", then the first slot)
LOAD="wait 8; key esc; wait 3; key esc; wait 3; click 0.5 0.875; wait 2; click 0.5 0.38; wait 5"
[ "${E2E_FROM_SAVE:-0}" = 1 ] && ACTIONS="$LOAD; $ACTIONS"
exec timeout 900 "$BIN/e2e_test" "$DEV/sys" "$DEV/saves" "$GAME" "$OUT" "$ACTIONS"
