/*
 *
 * Copyright (C) 2017 GlobalLogic
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstring>
#include <new>
#include <cerrno>
#include <memory>

#define LOG_TAG "Salvator GateKeeper HAL"
#include <utils/Log.h>
#include <utils/Errors.h>

#include <hardware/gatekeeper.h>

#include "optee_gatekeeper_device.h"

static int optee_gatekeeper_open(const hw_module_t *module, const char *name,
        hw_device_t **device) {
    using gatekeeper::OpteeGateKeeperDevice;

    ALOGI("Loading...");

    if (strcmp(name, HARDWARE_GATEKEEPER) != 0) {
        ALOGE("Wrong HAL name: %s", name);
        return android::BAD_VALUE;
    }

    std::unique_ptr<OpteeGateKeeperDevice> gatekeeper(
            new (std::nothrow) OpteeGateKeeperDevice(module));
    if (!gatekeeper) {
        ALOGE("Cannot create GateKeeper device, not enough memory");
        return android::NO_MEMORY;
    }

    if (!gatekeeper->connect()) {
        ALOGE("Failed to connect");
        return android::UNKNOWN_ERROR;
    }

    /*
     * gatekeeper object will be managed through hw_device_t's close function
     */
    *device = (gatekeeper.release())->hw_device();

    ALOGV("Completed");

    return android::OK;
}

static struct hw_module_methods_t gatekeeper_module_methods = {
    .open = optee_gatekeeper_open,
};

struct gatekeeper_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = GATEKEEPER_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = GATEKEEPER_HARDWARE_MODULE_ID,
        .name = "Salvator GateKeeper HAL",
        .author = "Renesas Electronics",
        .methods = &gatekeeper_module_methods,
        .dso = 0,
        .reserved = {}
    },
};
