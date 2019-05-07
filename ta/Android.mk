#
# Copyright (C) 2019 GlobalLogic
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
#

LOCAL_PATH:= $(call my-dir)

LOCAL_TA_GUID                  := 4d573443-6a56-4272-ac6f2425af9ef9bb

LOCAL_TA_OUT_INTERMEDIATES     := $(abspath $(PRODUCT_OUT)/obj/TA_OBJ)
LOCAL_OPTEE_OUT                := $(abspath $(PRODUCT_OUT)/obj/OPTEE_OBJ)

$(LOCAL_TA_GUID).ta:
	CROSS_COMPILE=$(BSP_GCC_CROSS_COMPILE) BINARY=$(TA_UUID-$@) TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) make -C $(TA_SRC-$@) O=$(TA_OUT-$@) clean
	CROSS_COMPILE=$(BSP_GCC_CROSS_COMPILE) BINARY=$(TA_UUID-$@) TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) make -C $(TA_SRC-$@) O=$(TA_OUT-$@) all

include $(CLEAR_VARS)
LOCAL_MODULE                := $(LOCAL_TA_GUID).ta
LOCAL_MODULE_TAGS           := optional
LOCAL_REQUIRED_MODULES      := tee.bin
include $(BUILD_PHONY_PACKAGE)

custom_myfile_target: 
        echo Run custom code here

include $(CLEAR_VARS)
LOCAL_MODULE := mymodule
LOCAL_ADDITIONAL_DEPENDENCIES := custom_myfile_target
include $(BUILD_PHONY_PACKAGE)
