/**
    Copyright Notice:
    Copyright 2021 DMTF. All rights reserved.
    License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libspdm/blob/main/LICENSE.md
**/

#include "spdm_requester_lib_internal.h"

/**
  Process the SPDM encapsulated CHALLENGE request and return the response.

  @param  spdm_context                  A pointer to the SPDM context.
  @param  request_size                  size in bytes of the request data.
  @param  request                      A pointer to the request data.
  @param  response_size                 size in bytes of the response data.
                                       On input, it means the size in bytes of response data buffer.
                                       On output, it means the size in bytes of copied response data buffer if RETURN_SUCCESS is returned,
                                       and means the size in bytes of desired response data buffer if RETURN_BUFFER_TOO_SMALL is returned.
  @param  response                     A pointer to the response data.

  @retval RETURN_SUCCESS               The request is processed and the response is returned.
  @retval RETURN_BUFFER_TOO_SMALL      The buffer is too small to hold the data.
  @retval RETURN_DEVICE_ERROR          A device error occurs when communicates with the device.
  @retval RETURN_SECURITY_VIOLATION    Any verification fails.
**/
return_status spdm_get_encap_response_challenge_auth(
	IN void *context, IN uintn request_size, IN void *request,
	IN OUT uintn *response_size, OUT void *response)
{
	spdm_challenge_request_t *spdm_request;
	spdm_challenge_auth_response_t *spdm_response;
	boolean result;
	uintn signature_size;
	uint8 slot_id;
	uint32 hash_size;
	uint32 measurement_summary_hash_size;
	uint8 *ptr;
	uintn total_size;
	spdm_context_t *spdm_context;
	spdm_challenge_auth_response_attribute_t auth_attribute;
	return_status status;

	spdm_context = context;
	spdm_request = request;

	if (!spdm_is_capabilities_flag_supported(
		    spdm_context, TRUE,
		    SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHAL_CAP, 0)) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_UNSUPPORTED_REQUEST,
			SPDM_CHALLENGE, response_size, response);
		return RETURN_SUCCESS;
	}

	if (request_size != sizeof(spdm_challenge_request_t)) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
			response_size, response);
		return RETURN_SUCCESS;
	}

	slot_id = spdm_request->header.param1;

	if ((slot_id != 0xFF) &&
	    (slot_id >= spdm_context->local_context.slot_count)) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
			response_size, response);
		return RETURN_SUCCESS;
	}

	//
	// Cache
	//
	status = spdm_append_message_mut_c(spdm_context, spdm_request,
					   request_size);
	if (RETURN_ERROR(status)) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
			response_size, response);
		return RETURN_SUCCESS;
	}

	signature_size = spdm_get_req_asym_signature_size(
		spdm_context->connection_info.algorithm.req_base_asym_alg);
	hash_size = spdm_get_hash_size(
		spdm_context->connection_info.algorithm.base_hash_algo);
	measurement_summary_hash_size = 0;

	total_size =
		sizeof(spdm_challenge_auth_response_t) + hash_size +
		SPDM_NONCE_SIZE + measurement_summary_hash_size +
		sizeof(uint16) +
		spdm_context->local_context.opaque_challenge_auth_rsp_size +
		signature_size;

	ASSERT(*response_size >= total_size);
	*response_size = total_size;
	zero_mem(response, *response_size);
	spdm_response = response;

	if (spdm_is_version_supported(spdm_context, SPDM_MESSAGE_VERSION_11)) {
		spdm_response->header.spdm_version = SPDM_MESSAGE_VERSION_11;
	} else {
		spdm_response->header.spdm_version = SPDM_MESSAGE_VERSION_10;
	}
	spdm_response->header.request_response_code = SPDM_CHALLENGE_AUTH;
	auth_attribute.slot_id = (uint8)(slot_id & 0xF);
	auth_attribute.reserved = 0;
	auth_attribute.basic_mut_auth_req = 0;
	spdm_response->header.param1 = *(uint8 *)&auth_attribute;
	spdm_response->header.param2 = (1 << slot_id);
	if (slot_id == 0xFF) {
		spdm_response->header.param2 = 0;

		slot_id = spdm_context->local_context.provisioned_slot_id;
	}

	ptr = (void *)(spdm_response + 1);
	spdm_generate_cert_chain_hash(spdm_context, slot_id, ptr);
	ptr += hash_size;

	spdm_get_random_number(SPDM_NONCE_SIZE, ptr);
	ptr += SPDM_NONCE_SIZE;

	ptr += measurement_summary_hash_size;

	*(uint16 *)ptr = (uint16)spdm_context->local_context
				 .opaque_challenge_auth_rsp_size;
	ptr += sizeof(uint16);
	copy_mem(ptr, spdm_context->local_context.opaque_challenge_auth_rsp,
		 spdm_context->local_context.opaque_challenge_auth_rsp_size);
	ptr += spdm_context->local_context.opaque_challenge_auth_rsp_size;

	//
	// Calc Sign
	//
	status = spdm_append_message_mut_c(spdm_context, spdm_response,
					   (uintn)ptr - (uintn)spdm_response);
	if (RETURN_ERROR(status)) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST, 0,
			response_size, response);
		return RETURN_SUCCESS;
	}
	result =
		spdm_generate_challenge_auth_signature(spdm_context, TRUE, ptr);
	if (!result) {
		spdm_generate_encap_error_response(
			spdm_context, SPDM_ERROR_CODE_UNSUPPORTED_REQUEST,
			SPDM_CHALLENGE_AUTH, response_size, response);
		return RETURN_SUCCESS;
	}
	ptr += signature_size;

	return RETURN_SUCCESS;
}