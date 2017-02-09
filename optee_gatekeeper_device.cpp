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

#include <cassert>
#include <type_traits>
#include <string>

#define LOG_TAG "OpteeGateKeeper"
#include <utils/Log.h>
#include <utils/Errors.h>

#include <gatekeeper_ipc.h>
#include "optee_gatekeeper_device.h"

namespace gatekeeper {

OpteeGateKeeperDevice::OpteeGateKeeperDevice(const hw_module_t *module)
    : connected(false)
{
    static_assert(std::is_standard_layout<OpteeGateKeeperDevice>::value,
                  "OpteeGateKeeperDevice must be standard layout");
    static_assert(offsetof(OpteeGateKeeperDevice, gatekeeperDevice) == 0,
                  "device_ must be the first member of OpteeGateKeeperDevice");
    static_assert(offsetof(OpteeGateKeeperDevice, gatekeeperDevice.common) == 0,
                  "common must be the first member of gatekeeper_device");

    memset(&gatekeeperDevice, 0, sizeof(gatekeeperDevice));
    gatekeeperDevice.common.tag = HARDWARE_DEVICE_TAG;
    gatekeeperDevice.common.version = 1;
    gatekeeperDevice.common.module = const_cast<hw_module_t *>(module);
    gatekeeperDevice.common.close = close_device;

    gatekeeperDevice.enroll = enroll;
    gatekeeperDevice.verify = verify;
    gatekeeperDevice.delete_user = nullptr;
    gatekeeperDevice.delete_all_users = nullptr;
}

OpteeGateKeeperDevice::~OpteeGateKeeperDevice()
{
    disconnect();
}

bool OpteeGateKeeperDevice::connect()
{
    if (connected) {
        ALOGE("Device is already connected");
        return false;
    }

    if (!gatekeeperIPC.connect(TA_GATEKEEPER_UUID)) {
        ALOGE("Fail to load Gatekeeper TA");
        return false;
    }
    connected = true;

    ALOGV("Connected");

    return true;
}

void OpteeGateKeeperDevice::disconnect()
{
    if (connected) {
        gatekeeperIPC.disconnect();
        connected = false;
    }

    ALOGV("Disconnected");
}

hw_device_t* OpteeGateKeeperDevice::hw_device()
{
    return &gatekeeperDevice.common;
}

int OpteeGateKeeperDevice::Enroll(uint32_t uid, const uint8_t *current_password_handle,
        uint32_t current_password_handle_length, const uint8_t *current_password,
        uint32_t current_password_length, const uint8_t *desired_password,
        uint32_t desired_password_length, uint8_t **enrolled_password_handle,
        uint32_t *enrolled_password_handle_length)
{
    ALOGV("Start enroll");

    if (!connected) {
        ALOGE("Device is not connected");
        return android::NO_INIT;
    }

    /*
     * Enroll request layout
     * +--------------------------------+---------------------------------+
     * | Name                           | Number of bytes                 |
     * +--------------------------------+---------------------------------+
     * | uid                            | 4                               |
     * | desired_password_length        | 4                               |
     * | desired_password               | #desired_password_length        |
     * | current_password_length        | 4                               |
     * | current_password               | #current_password_length        |
     * | current_password_handle_length | 4                               |
     * | current_password_handle        | #current_password_handle_length |
     * +--------------------------------+---------------------------------+
     */
    const uint32_t request_size = sizeof(uid) +
        sizeof(desired_password_length) +
        desired_password_length +
        sizeof(current_password_length) +
        current_password_length +
        sizeof(current_password_handle_length) +
        current_password_handle_length;
    uint8_t request[request_size];

    uint8_t *i_req = request;
    serialize_int(&i_req, uid);
    serialize_blob(&i_req, desired_password, desired_password_length);
    serialize_blob(&i_req, current_password, current_password_length);
    serialize_blob(&i_req, current_password_handle,
            current_password_handle_length);

    uint32_t response_size = RECV_BUF_SIZE;
    uint8_t response[response_size];

    if(!Send(GK_ENROLL, request, request_size, response, response_size)) {
        ALOGE("Enroll failed without respond");
        return android::UNKNOWN_ERROR;
    }

    const uint8_t *i_resp = response;
    uint32_t error;

    /*
     * Enroll response layout
     * +--------------------------------+---------------------------------+
     * | Name                           | Number of bytes                 |
     * +--------------------------------+---------------------------------+
     * | error                          | 4                               |
     * +--------------------------------+---------------------------------+
     * | retry_timeout                  | 4                               |
     * +------------------------------ OR --------------------------------+
     * | response_handle_length         | 4                               |
     * | response_handle                | #response_handle_length         |
     * +--------------------------------+---------------------------------+
     */
    deserialize_int(&i_resp, &error);
    if (error == ERROR_RETRY) {
        uint32_t retry_timeout;
        deserialize_int(&i_resp, &retry_timeout);
        ALOGV("Enroll returns retry timeout %u", retry_timeout);
        return retry_timeout;
    } else if (error != ERROR_NONE) {
        ALOGE("Enroll failed");
        return android::UNKNOWN_ERROR;
    }

    const uint8_t *response_handle;
    uint32_t response_handle_length;

    deserialize_blob(&i_resp, &response_handle, &response_handle_length);

    std::unique_ptr<uint8_t []> response_handle_ret(
            new (std::nothrow) uint8_t[response_handle_length]);
    if (!response_handle_ret) {
        ALOGE("Cannot create enrolled password handle, not enough memory");
        return android::NO_MEMORY;
    }

    memcpy(response_handle_ret.get(), response_handle, response_handle_length);

    *enrolled_password_handle = response_handle_ret.release();
    *enrolled_password_handle_length = response_handle_length;

    ALOGV("Enroll returns success");

    return android::OK;
}

int OpteeGateKeeperDevice::Verify(uint32_t uid, uint64_t challenge,
        const uint8_t *enrolled_password_handle, uint32_t enrolled_password_handle_length,
        const uint8_t *provided_password, uint32_t provided_password_length,
        uint8_t **auth_token, uint32_t *auth_token_length, bool *request_reenroll)
{
    if (!connected) {
        ALOGE("Device is not connected");
        return android::NO_INIT;
    }

    /*
     * Unused parameters
     */
    (void) &uid;
    (void) &challenge;
    (void) &enrolled_password_handle;
    (void) &enrolled_password_handle_length;
    (void) &provided_password;
    (void) &provided_password_length;
    (void) &auth_token;
    (void) &auth_token_length;
    (void) &request_reenroll;

    //TODO
    ALOGE("Verify request is currently not implemented");

    return -1;
}

bool OpteeGateKeeperDevice::Send(uint32_t command,
        const uint8_t *request, uint32_t request_size,
        uint8_t *response, uint32_t& response_size)
{
    return gatekeeperIPC.call(command, request, request_size,
            response, response_size);
}

static inline OpteeGateKeeperDevice *convert_device(const gatekeeper_device *dev)
{
    return reinterpret_cast<OpteeGateKeeperDevice *>(const_cast<gatekeeper_device *>(dev));
}

/* static */
int OpteeGateKeeperDevice::enroll(const struct gatekeeper_device *dev, uint32_t uid,
            const uint8_t *current_password_handle, uint32_t current_password_handle_length,
            const uint8_t *current_password, uint32_t current_password_length,
            const uint8_t *desired_password, uint32_t desired_password_length,
            uint8_t **enrolled_password_handle, uint32_t *enrolled_password_handle_length)
{
    if (dev == NULL ||
            enrolled_password_handle == NULL || enrolled_password_handle_length == NULL ||
            desired_password == NULL || desired_password_length == 0) {
        ALOGE("Enroll: Bad args");
        return android::BAD_VALUE;
    }

    // Current password and current password handle go together
    if (current_password_handle == NULL || current_password_handle_length == 0 ||
            current_password == NULL || current_password_length == 0) {
        current_password_handle = NULL;
        current_password_handle_length = 0;
        current_password = NULL;
        current_password_length = 0;
    }

    return convert_device(dev)->Enroll(uid, current_password_handle, current_password_handle_length,
            current_password, current_password_length, desired_password, desired_password_length,
            enrolled_password_handle, enrolled_password_handle_length);

}

/* static */
int OpteeGateKeeperDevice::verify(const struct gatekeeper_device *dev, uint32_t uid,
        uint64_t challenge, const uint8_t *enrolled_password_handle,
        uint32_t enrolled_password_handle_length, const uint8_t *provided_password,
        uint32_t provided_password_length, uint8_t **auth_token, uint32_t *auth_token_length,
        bool *request_reenroll)
{
    if (dev == NULL || enrolled_password_handle == NULL ||
            provided_password == NULL) {
        ALOGE("Verify: Bad args");
        return android::BAD_VALUE;
    }

    return convert_device(dev)->Verify(uid, challenge, enrolled_password_handle,
            enrolled_password_handle_length, provided_password, provided_password_length,
            auth_token, auth_token_length, request_reenroll);
}

/* static */
int OpteeGateKeeperDevice::close_device(hw_device_t* dev)
{
    delete reinterpret_cast<OpteeGateKeeperDevice *>(dev);
    return android::OK;
}

};
