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

#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <utee_defines.h>

#include "ta_gatekeeper.h"
#include "gatekeeper_ipc.h"
#include "failure_record.h"

static uint8_t	secret_ID[] = {0xB1, 0x6B, 0x00, 0xB5};

TEE_Result TA_CreateEntryPoint(void)
{
	TEE_Result		res = TEE_SUCCESS;
	TEE_ObjectHandle	secretObj = TEE_HANDLE_NULL;

	DMSG("Checking master key secret");
	res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE, secret_ID,
		sizeof(secret_ID), TEE_DATA_FLAG_ACCESS_READ, &secretObj);
	if (res == TEE_ERROR_ITEM_NOT_FOUND) {
		uint8_t secretData[HMAC_SHA256_KEY_SIZE_BYTE];
		DMSG("Create master key secret");

		TEE_GenerateRandom(secretData, sizeof(secretData));
		res = TEE_CreatePersistentObject(TEE_STORAGE_PRIVATE, secret_ID,
				sizeof(secret_ID), TEE_DATA_FLAG_ACCESS_WRITE,
				TEE_HANDLE_NULL, NULL, 0, &secretObj);
		if (res != TEE_SUCCESS) {
			EMSG("Failed to create secret");
		} else {
			res = TEE_WriteObjectData(secretObj, (void *)secretData,
					sizeof(secretData));
			if (res != TEE_SUCCESS) {
				EMSG("Failed to write secret data");
			}
			TEE_CloseObject(secretObj);
		}
	} else if (res == TEE_SUCCESS) {
		DMSG("Secret is already created");
		TEE_CloseObject(secretObj);
	} else {
		EMSG("Failed to open secret, error=%X", res);
	}

	return res;
}

void TA_DestroyEntryPoint(void)
{
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
		TEE_Param  params[TEE_NUM_PARAMS], void **sess_ctx)
{
	uint32_t exp_param_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE,
						   TEE_PARAM_TYPE_NONE);
	if (param_types != exp_param_types)
		return TEE_ERROR_BAD_PARAMETERS;

	/* Unused parameters */
	(void)&params;
	(void)&sess_ctx;

	return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx)
{
	/* Unused parameters */
	(void)&sess_ctx;
}

static TEE_Result TA_GetMasterKey(TEE_ObjectHandle masterKey)
{
	TEE_Result		res;
	TEE_Attribute		attrs[1];
	uint8_t			secretData[HMAC_SHA256_KEY_SIZE_BYTE];
	TEE_ObjectHandle	secretObj = TEE_HANDLE_NULL;
	uint32_t		readSize = 0;

	res = TEE_OpenPersistentObject(TEE_STORAGE_PRIVATE, secret_ID,
		sizeof(secret_ID), TEE_DATA_FLAG_ACCESS_READ, &secretObj);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to open secret, error=%X", res);
		goto exit;
	}

	res = TEE_ReadObjectData(secretObj, secretData, sizeof(secretData),
			&readSize);
	if (res != TEE_SUCCESS || sizeof(secretData) != readSize) {
		EMSG("Failed to read secret data, bytes = %u", readSize);
		goto close_obj;
	}

	TEE_InitRefAttribute(&attrs[0], TEE_ATTR_SECRET_VALUE, secretData,
			sizeof(secretData));

	res = TEE_PopulateTransientObject(masterKey, attrs,
			sizeof(attrs)/sizeof(attrs[0]));
	if (res != TEE_SUCCESS) {
		EMSG("Failed to set master key attributes");
		goto close_obj;
	}

close_obj:
	TEE_CloseObject(secretObj);
exit:
	return res;
}

static TEE_Result TA_ComputeSignature(uint8_t *signature, size_t signature_length,
		TEE_ObjectHandle key, const uint8_t *message, size_t length)
{
	uint32_t buf_length = HMAC_SHA256_KEY_SIZE_BYTE;
	uint8_t buf[buf_length];
	TEE_OperationHandle op = TEE_HANDLE_NULL;
	TEE_Result res;
	uint32_t to_write;

	res = TEE_AllocateOperation(&op, TEE_ALG_HMAC_SHA256, TEE_MODE_MAC,
			HMAC_SHA256_KEY_SIZE_BIT);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to allocate HMAC operation");
		goto exit;
	}

	res = TEE_SetOperationKey(op, key);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to set secret key");
		goto free_op;
	}

	TEE_MACInit(op, NULL, 0);

	TEE_MACComputeFinal(op, (void *)message, length, buf, &buf_length);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to compute HMAC");
		goto free_op;
	}

	to_write = buf_length;
	if (buf_length > signature_length)
		to_write = signature_length;

	memset(signature, 0, signature_length);
	memcpy(signature, buf, to_write);

free_op:
	TEE_FreeOperation(op);
exit:
	return res;
}

static TEE_Result TA_ComputePasswordSignature(
		uint8_t *signature, size_t signature_length,
		TEE_ObjectHandle key,
		const uint8_t *password, size_t password_length, salt_t salt)
{
	uint8_t salted_password[password_length + sizeof(salt)];
	memcpy(salted_password, &salt, sizeof(salt));
	memcpy(salted_password + sizeof(salt), password, password_length);
	return TA_ComputeSignature(signature, signature_length, key,
			salted_password, sizeof(salted_password));
}

static TEE_Result TA_CreatePasswordHandle(password_handle_t *password_handle,
		salt_t salt, secure_id_t user_id, uint64_t flags,
		uint64_t handle_version, const uint8_t *password,
		uint32_t password_length)
{
	password_handle_t pw_handle;
	const uint32_t metadata_length = sizeof(pw_handle.user_id) +
		sizeof(pw_handle.flags) +
		sizeof(pw_handle.version);
	uint8_t to_sign[password_length + metadata_length];

	TEE_ObjectHandle masterKey = TEE_HANDLE_NULL;
	TEE_Result res;

	res = TEE_AllocateTransientObject(TEE_TYPE_HMAC_SHA256,
			HMAC_SHA256_KEY_SIZE_BIT, &masterKey);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to allocate password key");
		goto exit;
	}

	pw_handle.version = handle_version;
	pw_handle.salt = salt;
	pw_handle.user_id = user_id;
	pw_handle.flags = flags;
	pw_handle.hardware_backed = true;

	memcpy(to_sign, &pw_handle, metadata_length);
	memcpy(to_sign + metadata_length, password, password_length);

	res = TA_GetMasterKey(masterKey);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to get master key");
		goto free_key;
	}

	res = TA_ComputePasswordSignature(pw_handle.signature,
			sizeof(pw_handle.signature), masterKey,
			to_sign, sizeof(to_sign), salt);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to compute password signature");
		goto free_key;
	}

	memcpy(password_handle, &pw_handle, sizeof(pw_handle));

free_key:
	TEE_FreeTransientObject(masterKey);
exit:
	return res;
}

static TEE_Result TA_Enroll(TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result res = TEE_SUCCESS;

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
	uint32_t uid;
	uint32_t desired_password_length;
	const uint8_t *desired_password;
	uint32_t current_password_length;
	const uint8_t *current_password;
	uint32_t current_password_handle_length;
	const uint8_t *current_password_handle;

	const uint8_t *request = (const uint8_t *)params[0].memref.buffer;
	const uint8_t *i_req = request;

	/*
	 * Enroll response layout
	 * +--------------------------------+---------------------------------+
	 * | Name                           | Number of bytes                 |
	 * +--------------------------------+---------------------------------+
	 * | error                          | 4                               |
	 * +--------------------------------+---------------------------------+
	 * | timeout                        | 4                               |
	 * +------------------------------ OR --------------------------------+
	 * | password_handle_length         | 4                               |
	 * | password_handle                | #password_handle_length         |
	 * +--------------------------------+---------------------------------+
	 */
	uint32_t error = ERROR_NONE;
	uint32_t timeout = 0;
	password_handle_t password_handle;

	uint8_t *response = params[1].memref.buffer;
	uint8_t *i_resp = response;

	const uint32_t max_response_size = sizeof(uint32_t) +
		sizeof(uint32_t) +
		sizeof(password_handle_t);

	secure_id_t user_id = 0;
	uint64_t flags = 0;
	salt_t salt;

	deserialize_int(&i_req, &uid);
	deserialize_blob(&i_req, &desired_password, &desired_password_length);
	deserialize_blob(&i_req, &current_password, &current_password_length);
	deserialize_blob(&i_req, &current_password_handle,
			&current_password_handle_length);

	// Check request buffer size
	if (get_size(request, i_req) > params[0].memref.size) {
		EMSG("Wrong request buffer size");
		res = TEE_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	// Check response buffer size
	if (max_response_size > params[1].memref.size) {
		EMSG("Wrong response buffer size");
		res = TEE_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	// Check password handle length
	if (current_password_handle_length != 0 &&
			current_password_handle_length != sizeof(password_handle_t)) {
		EMSG("Wrong password handle size");
		res = TEE_ERROR_BAD_PARAMETERS;
		goto exit;
	}

	if (!current_password_handle_length) {
		// Password handle does not match what is stored, generate new
		// secure user_id
		TEE_GenerateRandom(&user_id, sizeof(user_id));
	} else {
		//TODO Will be implement after verify
		return TEE_ERROR_NOT_IMPLEMENTED;
	}

	// TODO implement clear failure record

	TEE_GenerateRandom(&salt, sizeof(salt));
	res = TA_CreatePasswordHandle(&password_handle, salt, user_id, flags,
			HANDLE_VERSION, desired_password,
			desired_password_length);
	if (res != TEE_SUCCESS) {
		EMSG("Failed to create password handle");
		goto exit;
	}

serialize_response:
	serialize_int(&i_resp, error);
	switch (error) {
	case ERROR_INVALID:
	case ERROR_UNKNOWN:
		break;
	case ERROR_RETRY:
		serialize_int(&i_resp, timeout);
		break;
	case ERROR_NONE:
		serialize_blob(&i_resp, (const uint8_t *)&password_handle,
				sizeof(password_handle));
		break;
	default:
		EMSG("Unknown error message!");
		res = TEE_ERROR_GENERIC;
	}
	params[1].memref.size = get_size(response, i_resp);
exit:
	DMSG("Enroll returns 0x%08X, error = %d", res, error);
	return res;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx, uint32_t cmd_id,
			uint32_t param_types, TEE_Param params[TEE_NUM_PARAMS])
{
	if (TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
			TEE_PARAM_TYPE_MEMREF_OUTPUT,
			TEE_PARAM_TYPE_NONE,
			TEE_PARAM_TYPE_NONE) != param_types) {
		return TEE_ERROR_BAD_PARAMETERS;
	}

	DMSG("Gatekeeper TA invoke command cmd_id %u", cmd_id);

	switch (cmd_id) {
	case GK_ENROLL:
		return TA_Enroll(params);
	case GK_VERIFY:
		return TEE_ERROR_NOT_IMPLEMENTED;
	default:
		return TEE_ERROR_BAD_PARAMETERS;

	(void)&sess_ctx; /* Unused parameter */

	return TEE_ERROR_BAD_PARAMETERS;
}
