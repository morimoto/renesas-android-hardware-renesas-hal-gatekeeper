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

#ifndef TA_GATEKEEPER_H
#define TA_GATEKEEPER_H

#include <stdint.h>
#include <compiler.h>

/*
 * Please keep password_handle_t structure consistent with its counterpart
 * which defined in system/gatekeeper/include/gatekeeper/password_handle.h
 */

#define HANDLE_VERSION 2
#define HANDLE_VERSION_THROTTLE 2
#define HANDLE_FLAG_THROTTLE_SECURE 1

typedef uint64_t secure_id_t;
typedef uint64_t salt_t;

typedef struct __packed {
	uint8_t version;
	secure_id_t user_id;
	uint64_t flags;

	salt_t salt;
	uint8_t signature[32];

	bool hardware_backed;
} password_handle_t;



#define HMAC_SHA256_KEY_SIZE_BYTE 32
#define HMAC_SHA256_KEY_SIZE_BIT (8*HMAC_SHA256_KEY_SIZE_BYTE)


#endif /* TA_GATEKEEPER_H */
