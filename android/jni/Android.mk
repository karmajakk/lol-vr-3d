# lol-vr — application Quest. Les bibliothèques déjà compilées (cœur ScummVM, chargeur OpenXR)
# et leurs en-têtes sont copiés dans jni/prebuilt par scripts/build.sh.
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := retro
LOCAL_SRC_FILES := prebuilt/libretro.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := openxr_loader
LOCAL_SRC_FILES := prebuilt/libopenxr_loader.so
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/prebuilt/include
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := lolvr
LOCAL_SRC_FILES := ../../app/src/main_xr.cpp ../../app/src/core_host.cpp \
	../../app/src/scene.cpp ../../app/src/room.cpp ../../app/src/props.cpp ../../app/src/mesh.cpp \
	../../app/src/proctex.cpp ../../app/src/ambience.cpp ../../app/src/ui.cpp \
	../../app/src/dungeon.cpp ../../app/src/dungeon_data.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/prebuilt/include
LOCAL_CPPFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers
LOCAL_LDLIBS := -llog -landroid -lEGL -lGLESv3 -laaudio
# Point d'entrée de NativeActivity, fourni par android_native_app_glue.
LOCAL_LDFLAGS := -u ANativeActivity_onCreate
LOCAL_STATIC_LIBRARIES := android_native_app_glue
LOCAL_SHARED_LIBRARIES := retro openxr_loader
include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
