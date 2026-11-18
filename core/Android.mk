# lol-vr — compilation du cœur ScummVM (libretro) pour Quest.
# Ce fichier remplace backends/platform/libretro/jni/Android.mk dans les sources de ScummVM.
# Différences avec l'original : rendu logiciel uniquement, pas d'accès SAF, arm64 seulement.
LOCAL_PATH  := $(call my-dir)
ROOT_PATH   := $(LOCAL_PATH)/..
TARGET_NAME := scummvm
USE_IMGUI := 0
USE_LIBRETRO_SAF := 0
USE_LIBCO := 0
USE_FLUIDSYNTH := 0
USE_MT32EMU := 0
USE_SID_AUDIO := 0

# Reset flags not reset to Makefile.common
DEFINES   :=

ifneq (,$(findstring 64,$(TARGET_ARCH_ABI)))
  TARGET_64BIT := 1
endif

ifneq (,$(findstring arm64,$(TARGET_ARCH_ABI)))
  HAVE_NEON := 1
endif

include $(ROOT_PATH)/Makefile.common

include $(addprefix $(SCUMMVM_PATH)/, $(addsuffix /module.mk,$(MODULES)))

OBJS_MODULES := $(addprefix $(SCUMMVM_PATH)/, $(foreach MODULE,$(MODULES),$(MODULE_OBJS-$(MODULE))))

# Comme dans l'original : -O2 et _FORTIFY_SOURCE posent problème à certains moteurs.
COREFLAGS := $(DEFINES) -DUSE_CXX11 -O1 -U_FORTIFY_SOURCE -Wno-undefined-var-template

include $(CLEAR_VARS)
LOCAL_MODULE          := retro
LOCAL_MODULE_FILENAME := libretro
LOCAL_SRC_FILES       := $(DETECT_OBJS:%.o=$(SCUMMVM_PATH)/%.cpp)  $(OBJS_DEPS:%.o=%.c) $(OBJS_MODULES:%.o=%.cpp) $(OBJS:%.o=%.cpp)
LOCAL_C_INCLUDES      := $(subst -I,,$(INCLUDES))
LOCAL_CPPFLAGS        := $(COREFLAGS) -std=c++11
LOCAL_CFLAGS          := $(COREFLAGS)
LOCAL_LDFLAGS         := -Wl,-version-script=$(ROOT_PATH)/link.T
LOCAL_LDLIBS          := -lz -llog
LOCAL_CPP_FEATURES    := rtti
LOCAL_ARM_MODE        := arm
include $(BUILD_SHARED_LIBRARY)
