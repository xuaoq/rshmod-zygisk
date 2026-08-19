LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := rshmod
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_LIBRARIES)
LOCAL_CFLAGS := -O2 -fvisibility=hidden -fno-rtti -fexceptions
LOCAL_CPPFLAGS := -std=c++17
LOCAL_LDLIBS := -llog

LOCAL_SRC_FILES := \
    module.cpp \
    config.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)