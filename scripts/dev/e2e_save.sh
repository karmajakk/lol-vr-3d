#!/usr/bin/env bash
# Like e2e.sh, but starts in a saved game (slot 0) instead of replaying the intro: about 25 s
# instead of a minute or more. The save is made on first use: new game in English, Michael as
# the champion, saved in the throne room right away (slot 0, named "test").
# Usage: e2e_save.sh <out folder> "<actions>"      (E2E_NEW_SAVE=1 remakes the save)
# Do not save into slot 0 in your actions, or the next runs start from there.
source "$(dirname "$0")/common.sh"
OUT="$1"; ACTIONS="$2"
need_sysdir
SAVE="$DEV/saves/lol.000"
if [ ! -f "$SAVE" ] || [ "${E2E_NEW_SAVE:-0}" = 1 ]; then
	rm -f "$SAVE"
	echo "making the test save..."
	NEW="wait 8; key esc; wait 3; key esc; wait 3; click 0.5 0.74; wait 6; key esc; wait 4"     # main menu, new game
	NEW="$NEW; click 0.53 0.71; wait 3; click 0.34 0.94; wait 4"                                 # Michael, yes
	NEW="$NEW; key esc; wait 3; key esc; wait 3; key esc; wait 5"                                # skip the intro scenes
	NEW="$NEW; click 0.066 0.95; wait 2; click 0.5 0.36; wait 2; click 0.5 0.38; wait 1"         # floppy, save, first slot
	NEW="$NEW; key t; key e; key s; key t; key enter; wait 3"                                    # name "test"
	LOLVR_LANG=0 "$(dirname "$0")/e2e.sh" "$OUT" "$NEW" > /dev/null 2>&1 || true
	[ -f "$SAVE" ] || { echo "could not make the test save (game version or language differ?)"; exit 1; }
	echo "test save: $SAVE"
fi
E2E_FROM_SAVE=1 exec "$(dirname "$0")/e2e.sh" "$OUT" "$ACTIONS"
