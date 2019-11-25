#
# Copyright (C) 2017 GlobalLogic
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Include only for Renesas ones.
ifneq (,$(filter $(TARGET_PRODUCT), salvator ulcb kingfisher))

LOCAL_PATH := $(call my-dir)
TA_GATEKEEPER_SRC     := $(LOCAL_PATH)/ta
TA_GATEKEEPER_UUID    := 4d573443-6a56-4272-ac6f2425af9ef9bb

################################################################################
# Build gatekeeper HAL                                                         #
################################################################################
include $(CLEAR_VARS)
LOCAL_MODULE                := android.hardware.gatekeeper@1.0-service.renesas
LOCAL_INIT_RC               := android.hardware.gatekeeper@1.0-service.renesas.rc
LOCAL_VINTF_FRAGMENTS       := android.hardware.gatekeeper@1.0-service.renesas.xml
LOCAL_MODULE_RELATIVE_PATH  := hw
LOCAL_MODULE_TAGS           := optional
LOCAL_PROPRIETARY_MODULE    := true
LOCAL_REQUIRED_MODULES      := $(TA_GATEKEEPER_UUID)
LOCAL_CFLAGS                += -DANDROID_BUILD

LOCAL_SRC_FILES := \
    service.cpp \
    optee_gatekeeper_device.cpp \
    optee_ipc.cpp

LOCAL_C_INCLUDES := \
    vendor/renesas/utils/optee-client/public \
    $(TA_GATEKEEPER_SRC)/include

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libteec \
    libhardware \
    libhidlbase \
    libhidltransport \
    libutils \
    android.hardware.gatekeeper@1.0

include $(BUILD_EXECUTABLE)

################################################################################
# Build gatekeeper HAL TA                                                      #
################################################################################

# Please keep this variable consistent with TA_GATEKEEPER_UUID define that
# defined in gatekeeper_ipc.h file
TA_GATEKEEPER_OBJ           = $(PRODUCT_OUT)/obj/TA_OBJ/$(TA_GATEKEEPER_UUID)
TA_GATEKEEPER_OUT           = $(abspath $(TA_GATEKEEPER_OBJ))
TA_GATEKEEPER_BINARY        = $(TA_GATEKEEPER_OBJ)/$(TA_GATEKEEPER_UUID).ta
# OP-TEE Trusted OS is dependency for TA
OPTEE_BINARY                = $(PRODUCT_OUT)/obj/OPTEE_OBJ/core/tee.bin
OPTEE_TA_DEV_KIT_DIR        = $(abspath $(PRODUCT_OUT)/obj/OPTEE_OBJ/export-ta_arm64)

$(TA_GATEKEEPER_BINARY): $(OPTEE_BINARY) $(wildcard $(TA_GATEKEEPER_SRC)/*)
	mkdir -p $(TA_GATEKEEPER_OUT)
	CROSS_COMPILE=$(BSP_GCC_CROSS_COMPILE) BINARY=$(TA_GATEKEEPER_UUID) TA_DEV_KIT_DIR=$(OPTEE_TA_DEV_KIT_DIR) $(ANDROID_MAKE) -C $(TA_GATEKEEPER_SRC) O=$(TA_GATEKEEPER_OUT) clean
	CROSS_COMPILE=$(BSP_GCC_CROSS_COMPILE) BINARY=$(TA_GATEKEEPER_UUID) TA_DEV_KIT_DIR=$(OPTEE_TA_DEV_KIT_DIR) $(ANDROID_MAKE) -C $(TA_GATEKEEPER_SRC) O=$(TA_GATEKEEPER_OUT) all

include $(CLEAR_VARS)
LOCAL_MODULE                := $(TA_GATEKEEPER_UUID)
LOCAL_MODULE_STEM           := $(TA_GATEKEEPER_UUID).ta
LOCAL_PREBUILT_MODULE_FILE  := $(TA_GATEKEEPER_BINARY)
LOCAL_MODULE_PATH           := $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)/optee_armtz/
LOCAL_MODULE_CLASS          := EXECUTABLES
include $(BUILD_PREBUILT)

$(LOCAL_BUILT_MODULE): $(TA_GATEKEEPER_BINARY)

endif # Include only for Renesas ones.
