#!/usr/bin/env bash
# lol-vr-3d — construit l'APK pour Meta Quest.
#
# Prérequis : Android SDK (ANDROID_HOME) et NDK (ANDROID_NDK_HOME), git, curl, unzip, zip.
# Sur GitHub Actions, tout est déjà installé. Résultat : build/lol-vr-3d.apk
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SCUMMVM_TAG="v2026.4.0"
VERSION_NAME="${LOLVR_VERSION_NAME:-0.1-dev}"
VERSION_CODE="${LOLVR_VERSION_CODE:-1}"
JOBS="$(nproc)"

NDK="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
[ -n "$NDK" ] && [ -x "$NDK/ndk-build" ] || { echo "NDK introuvable (ANDROID_NDK_HOME)"; exit 1; }
[ -n "$SDK" ] && [ -d "$SDK/build-tools" ] || { echo "SDK introuvable (ANDROID_HOME)"; exit 1; }
BUILD_TOOLS="$SDK/build-tools/$(ls "$SDK/build-tools" | sort -V | tail -1)"
PLATFORM_JAR="$SDK/platforms/$(ls "$SDK/platforms" | sort -V | tail -1)/android.jar"
echo "NDK : $NDK"
echo "Outils : $BUILD_TOOLS"
echo "Plateforme : $PLATFORM_JAR"

mkdir -p "$BUILD"
PREBUILT="$ROOT/android/jni/prebuilt"
rm -rf "$PREBUILT"
mkdir -p "$PREBUILT/include"

step() { echo; echo "=== $* ==="; }

# ---------------------------------------------------------------------------
step "1/6 Sources de ScummVM ($SCUMMVM_TAG)"
SCUMMVM="$BUILD/scummvm"
if [ ! -d "$SCUMMVM/.git" ]; then
	git clone --depth 1 --branch "$SCUMMVM_TAG" https://github.com/scummvm/scummvm.git "$SCUMMVM"
	for p in "$ROOT"/patches/*.patch; do
		echo "Correctif : $(basename "$p")"
		git -C "$SCUMMVM" apply "$p"
	done
fi
LIBRETRO="$SCUMMVM/backends/platform/libretro"
cp "$ROOT/core/Android.mk" "$ROOT/core/Application.mk" "$LIBRETRO/jni/"
cp "$ROOT/core/lite_engines.list" "$LIBRETRO/lite_engines.list"

# ---------------------------------------------------------------------------
step "2/6 Chargeur OpenXR pour Android"
OPENXR="$BUILD/openxr"
if [ ! -f "$OPENXR/loader.aar" ]; then
	mkdir -p "$OPENXR"
	MAVEN="https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android"
	curl -fsSL "$MAVEN/maven-metadata.xml" -o "$OPENXR/metadata.xml"
	OPENXR_VERSION="$(grep -o '<release>[^<]*' "$OPENXR/metadata.xml" | sed 's/<release>//')"
	[ -n "$OPENXR_VERSION" ] || OPENXR_VERSION="$(grep -o '<version>[^<]*' "$OPENXR/metadata.xml" | sed 's/<version>//' | sort -V | tail -1)"
	echo "Version du chargeur OpenXR : $OPENXR_VERSION"
	curl -fsSL "$MAVEN/$OPENXR_VERSION/openxr_loader_for_android-$OPENXR_VERSION.aar" -o "$OPENXR/loader.aar"
	echo "$OPENXR_VERSION" > "$OPENXR/version.txt"
	(cd "$OPENXR" && unzip -q -o loader.aar -d aar)
fi
LOADER_SO="$(find "$OPENXR/aar" -name libopenxr_loader.so -path '*arm64-v8a*' | head -1)"
[ -n "$LOADER_SO" ] || { echo "libopenxr_loader.so (arm64) introuvable dans l'AAR"; find "$OPENXR/aar" | head -50; exit 1; }
cp "$LOADER_SO" "$PREBUILT/libopenxr_loader.so"
OPENXR_H="$(find "$OPENXR/aar" -name openxr.h | head -1)"
if [ -n "$OPENXR_H" ]; then
	mkdir -p "$PREBUILT/include/openxr"
	cp "$(dirname "$OPENXR_H")"/*.h "$PREBUILT/include/openxr/"
else
	echo "En-têtes absents de l'AAR : récupération depuis OpenXR-SDK"
	[ -d "$OPENXR/sdk" ] || git clone --depth 1 --branch "release-$(cat "$OPENXR/version.txt")" https://github.com/KhronosGroup/OpenXR-SDK.git "$OPENXR/sdk"
	mkdir -p "$PREBUILT/include/openxr"
	cp "$OPENXR/sdk/include/openxr"/*.h "$PREBUILT/include/openxr/"
fi

# ---------------------------------------------------------------------------
step "3/6 Dépendances du cœur et vérification rapide du code de l'application"
# Un premier passage de ndk-build à vide télécharge les dépendances du cœur (libretro-common),
# dont l'en-tête libretro.h nécessaire à l'application.
(cd "$LIBRETRO/jni" && "$NDK/ndk-build" LITE=1 -n >/dev/null 2>&1 || true)
LIBRETRO_H="$LIBRETRO/deps/libretro-common/include/libretro.h"
[ -f "$LIBRETRO_H" ] || { echo "libretro.h introuvable"; (cd "$LIBRETRO/jni" && "$NDK/ndk-build" LITE=1 -n | tail -40); exit 1; }
cp "$LIBRETRO_H" "$PREBUILT/include/"
# Compilation à blanc (sans édition de liens) : détecte les erreurs en quelques secondes,
# avant la longue compilation de ScummVM.
CLANG="$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/clang++ | head -1)"
for src in main_xr.cpp core_host.cpp scene.cpp room.cpp props.cpp mesh.cpp proctex.cpp ambience.cpp ui.cpp; do
	"$CLANG" --target=aarch64-linux-android29 -std=c++17 -fsyntax-only -Wall -Wextra -Wno-missing-field-initializers \
		-I"$PREBUILT/include" -I"$NDK/sources/android/native_app_glue" "$ROOT/app/src/$src"
done
echo "Code de l'application : OK"

# ---------------------------------------------------------------------------
step "4/6 Compilation du cœur ScummVM (Lands of Lore uniquement)"
(cd "$LIBRETRO/jni" && "$NDK/ndk-build" LITE=1 -j"$JOBS")
CORE_SO="$LIBRETRO/libs/arm64-v8a/libretro.so"
[ -f "$CORE_SO" ] || { echo "libretro.so introuvable"; exit 1; }
cp "$CORE_SO" "$PREBUILT/libretro.so"

# ---------------------------------------------------------------------------
step "5/6 Compilation de l'application"
(cd "$ROOT/android" && "$NDK/ndk-build" -j"$JOBS")
ls -la "$ROOT/android/libs/arm64-v8a/"

# ---------------------------------------------------------------------------
step "6/6 Assemblage et signature de l'APK"
APK_DIR="$BUILD/apk"
rm -rf "$APK_DIR" "$BUILD"/*.apk
mkdir -p "$APK_DIR/lib/arm64-v8a" "$APK_DIR/assets/scummvm/theme" "$APK_DIR/assets/scummvm/extra"
cp "$ROOT/android/libs/arm64-v8a/"*.so "$APK_DIR/lib/arm64-v8a/"
# Données de ScummVM : thèmes de l'interface, données du moteur Kyra, clavier virtuel.
cp "$SCUMMVM"/gui/themes/scummmodern.zip "$SCUMMVM"/gui/themes/scummclassic.zip "$SCUMMVM"/gui/themes/scummremastered.zip \
   "$SCUMMVM"/gui/themes/gui-icons.dat "$SCUMMVM"/gui/themes/translations.dat "$APK_DIR/assets/scummvm/theme/"
cp "$SCUMMVM"/dists/engine-data/kyra.dat "$SCUMMVM"/dists/engine-data/fonts.dat \
   "$SCUMMVM"/backends/vkeybd/packs/vkeybd_default.zip "$APK_DIR/assets/scummvm/extra/"

"$BUILD_TOOLS/aapt2" link -o "$BUILD/base.apk" --manifest "$ROOT/android/AndroidManifest.xml" -I "$PLATFORM_JAR" \
	--version-code "$VERSION_CODE" --version-name "$VERSION_NAME"
(cd "$APK_DIR" && zip -q -r "$BUILD/base.apk" lib assets)
"$BUILD_TOOLS/zipalign" -f -p 4 "$BUILD/base.apk" "$BUILD/aligned.apk"
"$BUILD_TOOLS/apksigner" sign --ks "$ROOT/keystore/debug.keystore" --ks-pass pass:android --key-pass pass:android \
	--ks-key-alias lolvr --out "$BUILD/lol-vr-3d.apk" "$BUILD/aligned.apk"
"$BUILD_TOOLS/apksigner" verify "$BUILD/lol-vr-3d.apk"
rm -f "$BUILD/base.apk" "$BUILD/aligned.apk" "$BUILD/lol-vr-3d.apk.idsig"
ls -la "$BUILD/lol-vr-3d.apk"
echo "APK prêt : build/lol-vr-3d.apk"
