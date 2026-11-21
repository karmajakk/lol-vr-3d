#!/usr/bin/env bash
# Builds the Linux test benches into $LOLVR_DEV/bin:
#   scene_test  the app's 3D scene and interface (no game needed): run "scene_test <out> uitest"
#   e2e_test    the real game through the core (needs build_core.sh first)
source "$(dirname "$0")/common.sh"
need_khronos
cd "$ROOT"
GLLIBS="/usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers $GL_INCLUDES \
	app/test/scene_test.cpp app/src/ui.cpp app/src/scene.cpp app/src/room.cpp app/src/props.cpp app/src/mesh.cpp \
	app/src/proctex.cpp app/src/dungeon.cpp app/src/dungeon_data.cpp \
	-o "$BIN/scene_test" $GLLIBS -lpthread
echo "built $BIN/scene_test"
if [ -f "$CORE" ]; then
	g++ -std=c++17 -O1 -Wall -Wno-missing-field-initializers -Iapp/src $GL_INCLUDES \
		-I"$LIBRETRO/deps/libretro-common/include" \
		app/test/e2e_test.cpp app/src/core_host.cpp app/src/dungeon.cpp app/src/dungeon_data.cpp app/src/ui.cpp app/src/room.cpp app/src/props.cpp app/src/mesh.cpp app/src/proctex.cpp \
		-o "$BIN/e2e_test" "$CORE" $GLLIBS -lpthread -Wl,-rpath,"$LIBRETRO"
	echo "built $BIN/e2e_test"
else
	echo "no core yet: run build_core.sh, then this again, for e2e_test"
fi
