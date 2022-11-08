/* Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */

#include "SampleAduJWS.h"

#include <az_core.h>
#include <az_iot.h>

#include "mbedtls/base64.h"
#include "mbedtls/cipher.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"

/* For logging */
#include "SerialLogger.h"

/**
 * @brief Convenience macro to return if an operation failed.
 */
#define _az_adu_jws_return_if_failed(exp) \
  do                                      \
  {                                       \
    az_result const _azResult = (exp);    \
    if (_azResult != AZ_OK)               \
    {                                     \
      return _azResult;                   \
    }                                     \
  } while (0)

const az_span jws_sha256_json_value = AZ_SPAN_FROM_STR("sha256");
const az_span jws_sjwk_json_value = AZ_SPAN_FROM_STR("sjwk");
const az_span jws_kid_json_value = AZ_SPAN_FROM_STR("kid");
const az_span jws_n_json_value = AZ_SPAN_FROM_STR("n");
const az_span jws_e_json_value = AZ_SPAN_FROM_STR("e");
const az_span jws_alg_json_value = AZ_SPAN_FROM_STR("alg");
const az_span jws_alg_rs256 = AZ_SPAN_FROM_STR("RS256");

typedef struct prvJWSValidationContext
{
  az_span jwk_header;
  az_span jwk_payload;
  az_span jwk_signature;
  az_span scratch_calculation_buffer;
  az_span jws_header;
  az_span jws_payload;
  az_span jws_signature;
  az_span signing_key_n;
  az_span signing_key_e;
  az_span manifest_sha_calculation;
  az_span parsed_manifest_sha;
  az_span base64_encoded_header;
  az_span base64_encoded_payload;
  az_span base64_encoded_signature;
  az_span jwk_base64_encoded_header;
  az_span jwk_base64_encoded_payload;
  az_span jwk_base64_encoded_signature;
  int32_t base64_signature_length;
  int32_t out_parsed_manifest_sha_size;
  int32_t out_signing_key_e_length;
  int32_t out_signing_key_n_length;
  int32_t out_jws_header_length;
  int32_t out_jws_payload_length;
  int32_t out_jws_signature_length;
  int32_t out_jwk_header_length;
  int32_t out_jwk_payload_length;
  int32_t out_jwk_signature_length;
  az_span kid_span;
  az_span sha256_span;
  az_span base64_encoded_n_span;
  az_span base64_encoded_e_span;
  az_span alg_span;
  az_span jwk_manifest_span;
} jws_validation_context;

/* split_jws takes a JWS payload and returns pointers to its constituent header,
 * payload, and signature parts. */
static az_result split_jws(
    az_span jws_span,
    az_span* header_span,
    az_span* payload_span,
    az_span* signature_span)
{
  uint8_t* first_dot;
  uint8_t* second_dot;
  int32_t dot_count = 0;
  int32_t index = 0;
  int32_t jws_length = az_span_size(jws_span);
  uint8_t* jws_ptr = az_span_ptr(jws_span);

  while (index < jws_length)
  {
    if (*jws_ptr == '.')
    {
      dot_count++;

      if (dot_count == 1)
      {
        first_dot = jws_ptr;
      }
      else if (dot_count == 2)
      {
        second_dot = jws_ptr;
      }
      else if (dot_count > 2)
      {
        Logger.Error("JWS had more '.' than required (2)");
        return AZ_ERROR_UNEXPECTED_CHAR;
      }
    }

    jws_ptr++;
    index++;
  }

  if ((dot_count != 2) || (second_dot >= (az_span_ptr(jws_span) + jws_length - 1)))
  {
    return AZ_ERROR_UNEXPECTED_CHAR;
  }

  *header_span = az_span_create(az_span_ptr(jws_span), first_dot - az_span_ptr(jws_span));
  *payload_span = az_span_create(first_dot + 1, second_dot - first_dot - 1);
  *signature_span
      = az_span_create(second_dot + 1, az_span_ptr(jws_span) + jws_length - second_dot - 1);

  return AZ_OK;
}

/* Usual base64 encoded characters use `+` and `/` for the two extra characters
 */
/* In URL encoded schemes, those aren't allowed, so the characters are swapped
 */
/* for `-` and `_`. We have to swap them back to the usual characters. */
static void swap_to_url_encoding_chars(az_span signature_span)
{
  int32_t index = 0;
  uint8_t* signature_ptr = az_span_ptr(signature_span);
  int32_t signature_length = az_span_size(signature_span);

  while (index < signature_length)
  {
    if (*signature_ptr == '-')
    {
      *signature_ptr = '+';
    }
    else if (*signature_ptr == '_')
    {
      *signature_ptr = '/';
    }

    signature_ptr++;
    index++;
  }
}

/**
 * @brief Calculate the SHA256 over a buffer of bytes
 *
 * @param input_span The input span over which to calculate the SHA256.
 * @param output_span The output span into which the SHA256. It must be 32 bytes
 * in length.
 * @return az_result The result of the operation.
 * @retval AZ_OK if successful.
 */
static az_result jws_sha256_calculate(az_span input_span, az_span output_span)
{
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, az_span_ptr(input_span), az_span_size(input_span));
  mbedtls_md_finish(&ctx, az_span_ptr(output_span));
  mbedtls_md_free(&ctx);

  return AZ_OK;
}

/**
 * @brief Verify the manifest via RS256 for the JWS.
 *
 * @param input_span The input span over which the RS256 will be verified.
 * @param signature_span The encrypted signature span which will be decrypted by \p
 * n_span and \p e_span.
 * @param n_span The key's modulus which is used to decrypt \p signature.
 * @param e_span The exponent used for the key.
 * @param buffer_span The buffer used as scratch space to make the calculations.
 * It should be at least `jwsRSA3072_SIZE` + `jwsSHA256_SIZE` in size.
 * @return az_result The result of the operation.
 * @retval AZ_OK if successful.
 */
static az_result jws_rs256_verify(
    az_span input_span,
    az_span signature_span,
    az_span n_span,
    az_span e_span,
    az_span buffer_span)
{
  az_result result;
  int32_t mbed_tls_result;
  uint8_t* sha_buffer_ptr;
  size_t decrypted_length;
  mbedtls_rsa_context ctx;
  int sha_match_result;

  if (az_span_size(buffer_span) < jwsSHA_CALCULATION_SCRATCH_SIZE)
  {
    Logger.Error("[JWS] Buffer Not Large Enough");
    return AZ_ERROR_NOT_ENOUGH_SPACE;
  }

  sha_buffer_ptr = az_span_ptr(buffer_span) + jwsRSA3072_SIZE;

  /* The signature is encrypted using the input key. We need to decrypt the */
  /* signature which gives us the SHA256 inside a PKCS7 structure. We then
   * compare */
  /* that to the SHA256 of the input. */
  mbedtls_rsa_init(&ctx, MBEDTLS_RSA_PKCS_V15, 0);

  mbed_tls_result = mbedtls_rsa_import_raw(
      &ctx,
      az_span_ptr(n_span),
      az_span_size(n_span),
      NULL,
      0,
      NULL,
      0,
      NULL,
      0,
      az_span_ptr(e_span),
      az_span_size(e_span));

  if (mbed_tls_result != 0)
  {
    Logger.Error("[JWS] mbedtls_rsa_import_raw res: " + String(mbed_tls_result));
    mbedtls_rsa_free(&ctx);
    return AZ_ERROR_NOT_SUPPORTED;
  }

  mbed_tls_result = mbedtls_rsa_complete(&ctx);

  if (mbed_tls_result != 0)
  {
    Logger.Error("[JWS] mbedtls_rsa_complete res: " + String(mbed_tls_result));
    mbedtls_rsa_free(&ctx);
    return AZ_ERROR_NOT_SUPPORTED;
  }

  mbed_tls_result = mbedtls_rsa_check_pubkey(&ctx);

  if (mbed_tls_result != 0)
  {
    Logger.Error("[JWS] mbedtls_rsa_check_pubkey res: " + String(mbed_tls_result));
    mbedtls_rsa_free(&ctx);
    return AZ_ERROR_NOT_SUPPORTED;
  }

  /* RSA */
  mbed_tls_result = mbedtls_rsa_pkcs1_decrypt(
      &ctx,
      NULL,
      NULL,
      MBEDTLS_RSA_PUBLIC,
      &decrypted_length,
      az_span_ptr(signature_span),
      az_span_ptr(buffer_span),
      jwsRSA3072_SIZE);

  if (mbed_tls_result != 0)
  {
    Logger.Error("[JWS] mbedtls_rsa_pkcs1_decrypt res: " + String(mbed_tls_result));
    mbedtls_rsa_free(&ctx);
    return AZ_ERROR_NOT_SUPPORTED;
  }

  mbedtls_rsa_free(&ctx);

  result = jws_sha256_calculate(input_span, az_span_create(sha_buffer_ptr, jwsSHA256_SIZE));

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] jws_sha256_calculate failed");
    return result;
  }

  /* TODO: remove this once we have a valid PKCS7 parser. */
  sha_match_result
      = memcmp(az_span_ptr(buffer_span) + jwsPKCS7_PAYLOAD_OFFSET, sha_buffer_ptr, jwsSHA256_SIZE);

  if (sha_match_result)
  {
    Logger.Error("[JWS] SHA of JWK does NOT match");
    result = AZ_ERROR_NOT_SUPPORTED;
  }

  return AZ_OK;
}

static az_result find_sjwk_value(az_json_reader* payload_json_reader, az_span* jwk_value_ptr)
{
  az_result result = AZ_OK;

  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));

  while (result == AZ_OK)
  {
    if (az_json_token_is_text_equal(&payload_json_reader->token, jws_sjwk_json_value))
    {
      /* Found name, move to value */
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      break;
    }
    else
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      _az_adu_jws_return_if_failed(az_json_reader_skip_children(payload_json_reader));
      result = az_json_reader_next_token(payload_json_reader);
    }
  }

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] Parse JSK JSON Payload Error: " + String(result, HEX));
    return result;
  }

  if (payload_json_reader->token.kind != AZ_JSON_TOKEN_STRING)
  {
    Logger.Error("[JWS] JSON token type wrong | type: " + String(payload_json_reader->token.kind));
    return AZ_ERROR_JSON_INVALID_STATE;
  }

  *jwk_value_ptr = payload_json_reader->token.slice;

  return AZ_OK;
}

static az_result find_root_key_value(az_json_reader* payload_json_reader, az_span* kid_span_ptr)
{
  az_result result = AZ_OK;

  /*Begin object */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
  /*Property Name */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));

  while (result == AZ_OK)
  {
    if (az_json_token_is_text_equal(&payload_json_reader->token, jws_kid_json_value))
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      *kid_span_ptr = payload_json_reader->token.slice;

      break;
    }
    else
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      _az_adu_jws_return_if_failed(az_json_reader_skip_children(payload_json_reader));
      result = az_json_reader_next_token(payload_json_reader);
    }
  }

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] Parse Root Key Error: " + String(result, HEX));
    return result;
  }

  if (payload_json_reader->token.kind != AZ_JSON_TOKEN_STRING)
  {
    Logger.Error("[JWS] JSON token type wrong | type: " + String(payload_json_reader->token.kind));
    return AZ_ERROR_JSON_INVALID_STATE;
  }

  return AZ_OK;
}

static az_result find_key_parts(
    az_json_reader* payload_json_reader,
    az_span* base64_encoded_n_span_ptr,
    az_span* base64_encoded_e_span_ptr,
    az_span* alg_span_ptr)
{
  az_result result = AZ_OK;

  /*Begin object */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
  /*Property Name */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));

  while (result == AZ_OK
         && (az_span_size(*base64_encoded_n_span_ptr) == 0
             || az_span_size(*base64_encoded_e_span_ptr) == 0 || az_span_size(*alg_span_ptr) == 0))
  {
    if (az_json_token_is_text_equal(&payload_json_reader->token, jws_n_json_value))
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      *base64_encoded_n_span_ptr = payload_json_reader->token.slice;

      result = az_json_reader_next_token(payload_json_reader);
    }
    else if (az_json_token_is_text_equal(&payload_json_reader->token, jws_e_json_value))
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      *base64_encoded_e_span_ptr = payload_json_reader->token.slice;

      result = az_json_reader_next_token(payload_json_reader);
    }
    else if (az_json_token_is_text_equal(&payload_json_reader->token, jws_alg_json_value))
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      *alg_span_ptr = payload_json_reader->token.slice;

      result = az_json_reader_next_token(payload_json_reader);
    }
    else
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      _az_adu_jws_return_if_failed(az_json_reader_skip_children(payload_json_reader));
      result = az_json_reader_next_token(payload_json_reader);
    }
  }

  if ((result != AZ_OK) || (az_span_size(*base64_encoded_n_span_ptr) == 0)
      || (az_span_size(*base64_encoded_e_span_ptr) == 0) || (az_span_size(*alg_span_ptr) == 0))
  {
    Logger.Error("[JWS] Parse Signing Key Payload Error: " + String(result, HEX));
    return result;
  }

  return AZ_OK;
}

static az_result find_manifest_sha(az_json_reader* payload_json_reader, az_span* sha_span_ptr)
{
  az_result result = AZ_OK;

  /*Begin object */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
  /*Property Name */
  _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));

  while (result == AZ_OK)
  {
    if (az_json_token_is_text_equal(&payload_json_reader->token, jws_sha256_json_value))
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      break;
    }
    else
    {
      _az_adu_jws_return_if_failed(az_json_reader_next_token(payload_json_reader));
      _az_adu_jws_return_if_failed(az_json_reader_skip_children(payload_json_reader));
      result = az_json_reader_next_token(payload_json_reader);
    }
  }

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] Parse manifest SHA error: " + String(result, HEX));
    return result;
  }

  if (payload_json_reader->token.kind != AZ_JSON_TOKEN_STRING)
  {
    Logger.Error("[JWS] JSON token type wrong | type: " + String(payload_json_reader->token.kind));
    return AZ_ERROR_JSON_INVALID_STATE;
  }

  *sha_span_ptr = payload_json_reader->token.slice;

  return AZ_OK;
}

static az_result base64_decode_jwk(jws_validation_context* manifest_context)
{
  az_result result;

  result = az_base64_url_decode(
      manifest_context->jwk_header,
      manifest_context->jwk_base64_encoded_header,
      &manifest_context->out_jwk_header_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWK header az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsJWK_HEADER_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->jwk_header
      = az_span_slice(manifest_context->jwk_header, 0, manifest_context->out_jwk_header_length);

  result = az_base64_url_decode(
      manifest_context->jwk_payload,
      manifest_context->jwk_base64_encoded_payload,
      &manifest_context->out_jwk_payload_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWK payload az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsJWK_PAYLOAD_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->jwk_payload
      = az_span_slice(manifest_context->jwk_payload, 0, manifest_context->out_jwk_payload_length);

  result = az_base64_url_decode(
      manifest_context->jwk_signature,
      manifest_context->jwk_base64_encoded_signature,
      &manifest_context->out_jwk_signature_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWK signature az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsSIGNATURE_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->jwk_signature = az_span_slice(
      manifest_context->jwk_signature, 0, manifest_context->out_jwk_signature_length);

  return AZ_OK;
}

static az_result base64_decode_signing_key(jws_validation_context* manifest_context)
{
  az_result result;

  result = az_base64_decode(
      manifest_context->signing_key_n,
      manifest_context->base64_encoded_n_span,
      &manifest_context->out_signing_key_n_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] Signing key n az_base64_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsRSA3072_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->signing_key_n = az_span_slice(
      manifest_context->signing_key_n, 0, manifest_context->out_signing_key_n_length);

  result = az_base64_decode(
      manifest_context->signing_key_e,
      manifest_context->base64_encoded_e_span,
      &manifest_context->out_signing_key_e_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] Signing key e az_base64_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error(
          "[JWS] Decode buffer was too small: " + String(jwsSIGNING_KEY_E_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->signing_key_e = az_span_slice(
      manifest_context->signing_key_e, 0, manifest_context->out_signing_key_e_length);

  return AZ_OK;
}

static az_result base64_decode_jws_header_and_payload(jws_validation_context* manifest_context)
{
  az_result result;

  result = az_base64_url_decode(
      manifest_context->jws_payload,
      manifest_context->base64_encoded_payload,
      &manifest_context->out_jws_payload_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWS payload az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsJWS_PAYLOAD_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->jws_payload
      = az_span_slice(manifest_context->jws_payload, 0, manifest_context->out_jws_payload_length);

  result = az_base64_url_decode(
      manifest_context->jws_signature,
      manifest_context->base64_encoded_signature,
      &manifest_context->out_jws_signature_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWS signature az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsSIGNATURE_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->jws_signature = az_span_slice(
      manifest_context->jws_signature, 0, manifest_context->out_jws_signature_length);

  return AZ_OK;
}

static az_result validate_root_key(
    jws_validation_context* manifest_context,
    SampleJWS::RootKey* root_keys,
    uint32_t root_keys_length,
    int32_t* adu_root_key_index)
{
  az_result result;
  az_json_reader json_reader;

  result = az_json_reader_init(&json_reader, manifest_context->jwk_header, NULL);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] az_json_reader_init failed: result " + String(result, HEX));

    return result;
  }

  if (find_root_key_value(&json_reader, &manifest_context->kid_span) != AZ_OK)
  {
    Logger.Error("Could not find kid in JSON");
    return AZ_ERROR_ITEM_NOT_FOUND;
  }

  for (int i = 0; i < root_keys_length; i++)
  {
    if (az_span_is_content_equal(root_keys[i].root_key_id, manifest_context->kid_span))
    {
      *adu_root_key_index = i;
      return AZ_OK;
    }
  }

  return AZ_ERROR_NOT_SUPPORTED;
}

static az_result verify_sha_match(jws_validation_context* manifest_context, az_span manifest_span)
{
  az_json_reader json_reader;
  az_result verification_result;
  az_result result;

  verification_result
      = jws_sha256_calculate(manifest_span, manifest_context->manifest_sha_calculation);

  if (verification_result != AZ_OK)
  {
    Logger.Error("[JWS] SHA256 Calculation failed");
    return verification_result;
  }

  result = az_json_reader_init(&json_reader, manifest_context->jws_payload, NULL);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] az_json_reader_init failed: result " + String(result, HEX));

    return result;
  }

  if (find_manifest_sha(&json_reader, &manifest_context->sha256_span) != AZ_OK)
  {
    Logger.Error("Error finding manifest signature SHA");
    return AZ_ERROR_ITEM_NOT_FOUND;
  }

  result = az_base64_decode(
      manifest_context->parsed_manifest_sha,
      manifest_context->sha256_span,
      &manifest_context->out_parsed_manifest_sha_size);

  if (az_result_failed(result))
  {
    Logger.Error(
        "[JWS] Parsed manifest SHA az_base64_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsSHA256_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context->parsed_manifest_sha = az_span_slice(
      manifest_context->parsed_manifest_sha, 0, manifest_context->out_parsed_manifest_sha_size);

  if (manifest_context->out_parsed_manifest_sha_size != jwsSHA256_SIZE)
  {
    Logger.Error(
        "[JWS] Base64 decoded SHA256 is not the correct length | expected: "
        + String(jwsSHA256_SIZE)
        + " | actual: " + String(manifest_context->out_parsed_manifest_sha_size));
    return AZ_ERROR_ITEM_NOT_FOUND;
  }

  int32_t comparison_result = memcmp(
      az_span_ptr(manifest_context->manifest_sha_calculation),
      az_span_ptr(manifest_context->parsed_manifest_sha),
      jwsSHA256_SIZE);

  if (comparison_result != 0)
  {
    Logger.Error("[JWS] Calculated manifest SHA does not match SHA in payload");
    return AZ_ERROR_NOT_SUPPORTED;
  }
  else
  {
    Logger.Info(("[JWS] Calculated manifest SHA matches parsed SHA"));
  }

  return AZ_OK;
}

az_result SampleJWS::ManifestAuthenticate(
    az_span manifest_span,
    az_span jws_span,
    SampleJWS::RootKey* root_keys,
    uint32_t root_keys_length,
    az_span scratch_buffer_span)
{
  az_result result;
  az_json_reader json_reader;
  jws_validation_context manifest_context = { 0 };
  int32_t root_key_index;

  /* Break up scratch buffer for reusable and persistent sections */
  uint8_t* persistent_scratch_space_head = az_span_ptr(scratch_buffer_span);
  uint8_t* reusable_scratch_space_root
      = persistent_scratch_space_head + jwsJWS_HEADER_SIZE + jwsJWK_PAYLOAD_SIZE;
  uint8_t* reusable_scratch_space_head = reusable_scratch_space_root;

  /*------------------- Parse and Decode the JWS Header
   * ------------------------*/

  result = split_jws(
      jws_span,
      &manifest_context.base64_encoded_header,
      &manifest_context.base64_encoded_payload,
      &manifest_context.base64_encoded_signature);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] split_jws failed");
    return result;
  }

  /* Note that we do not use mbedTLS to base64 decode values since we need the
   * ability to assume padding characters. */
  /* mbedTLS will stop the decoding short and we would then need to add in the
   * remaining characters. */
  manifest_context.jws_header = az_span_create(persistent_scratch_space_head, jwsJWS_HEADER_SIZE);
  persistent_scratch_space_head += jwsJWS_HEADER_SIZE;
  result = az_base64_url_decode(
      manifest_context.jws_header,
      manifest_context.base64_encoded_header,
      &manifest_context.out_jws_header_length);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] JWS header az_base64_url_decode failed: result " + String(result, HEX));

    if (result == AZ_ERROR_NOT_ENOUGH_SPACE)
    {
      Logger.Error("[JWS] Decode buffer was too small: " + String(jwsJWS_HEADER_SIZE) + " bytes");
    }

    return result;
  }

  manifest_context.jws_header
      = az_span_slice(manifest_context.jws_header, 0, manifest_context.out_jws_header_length);

  /*------------------- Parse SJWK JSON Payload ------------------------*/

  /* The "sjwk" is the signed signing public key */
  result = az_json_reader_init(&json_reader, manifest_context.jws_header, NULL);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] az_json_reader_init failed: result " + String(result, HEX));
    return result;
  }

  result = find_sjwk_value(&json_reader, &manifest_context.jwk_manifest_span);
  if (az_result_failed(result))
  {
    Logger.Error("Error finding sjwk value in payload");
    return AZ_ERROR_ITEM_NOT_FOUND;
  }

  /*------------------- Split JWK and Base64 Decode the JWK Payload
   * ------------------------*/

  result = split_jws(
      manifest_context.jwk_manifest_span,
      &manifest_context.jwk_base64_encoded_header,
      &manifest_context.jwk_base64_encoded_payload,
      &manifest_context.jwk_base64_encoded_signature);

  if (az_result_failed(result))
  {
    Logger.Error("[JWS] split_jws failed");
    return result;
  }

  manifest_context.jwk_header = az_span_create(reusable_scratch_space_head, jwsJWK_HEADER_SIZE);
  reusable_scratch_space_head += jwsJWK_HEADER_SIZE;
  /* Needs to be persisted so we can parse the signing key N and E later */
  manifest_context.jwk_payload = az_span_create(persistent_scratch_space_head, jwsJWK_PAYLOAD_SIZE);
  persistent_scratch_space_head += jwsJWK_PAYLOAD_SIZE;
  manifest_context.jwk_signature = az_span_create(reusable_scratch_space_head, jwsSIGNATURE_SIZE);
  reusable_scratch_space_head += jwsSIGNATURE_SIZE;

  result = base64_decode_jwk(&manifest_context);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] base64_decode_jwk failed: result " + String(result, HEX));
    return result;
  }

  /*------------------- Parse root key id ------------------------*/

  result = validate_root_key(&manifest_context, root_keys, root_keys_length, &root_key_index);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] validate_root_key failed: result " + String(result, HEX));
    return result;
  }

  /*------------------- Parse necessary pieces for signing key
   * ------------------------*/

  result = az_json_reader_init(&json_reader, manifest_context.jwk_payload, NULL);
  if (az_result_failed(result))
  {
    Logger.Error("[JWS] az_json_reader_init failed: result " + String(result, HEX));
    return result;
  }

  if (find_key_parts(
          &json_reader,
          &manifest_context.base64_encoded_n_span,
          &manifest_context.base64_encoded_e_span,
          &manifest_context.alg_span)
      != AZ_OK)
  {
    Logger.Error("Could not find parts for the signing key");
    return AZ_ERROR_ITEM_NOT_FOUND;
  }

  /*------------------- Verify the signature ------------------------*/

  manifest_context.scratch_calculation_buffer
      = az_span_create(reusable_scratch_space_head, jwsSHA_CALCULATION_SCRATCH_SIZE);
  reusable_scratch_space_head += jwsSHA_CALCULATION_SCRATCH_SIZE;
  result = jws_rs256_verify(
      az_span_create(
          az_span_ptr(manifest_context.jwk_base64_encoded_header),
          az_span_size(manifest_context.jwk_base64_encoded_header)
              + az_span_size(manifest_context.jwk_base64_encoded_payload) + 1),
      manifest_context.jwk_signature,
      root_keys[root_key_index].root_key_n,
      root_keys[root_key_index].root_key_exponent,
      manifest_context.scratch_calculation_buffer);

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] jws_rs256_verify failed");
    return result;
  }

  /*------------------- Reuse Buffer Space ------------------------*/

  /* The JWK verification is now done, so we can reuse the buffers which it
   * used. */
  reusable_scratch_space_head = reusable_scratch_space_root;

  /*------------------- Decode remaining values from JWS
   * ------------------------*/

  manifest_context.jws_payload = az_span_create(reusable_scratch_space_head, jwsJWS_PAYLOAD_SIZE);
  reusable_scratch_space_head += jwsJWS_PAYLOAD_SIZE;
  manifest_context.jws_signature = az_span_create(reusable_scratch_space_head, jwsSIGNATURE_SIZE);
  reusable_scratch_space_head += jwsSIGNATURE_SIZE;

  result = base64_decode_jws_header_and_payload(&manifest_context);

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] base64_decode_jws_header_and_payload failed");
    return result;
  }

  /*------------------- Base64 decode the signing key ------------------------*/

  manifest_context.signing_key_n = az_span_create(reusable_scratch_space_head, jwsRSA3072_SIZE);
  reusable_scratch_space_head += jwsRSA3072_SIZE;
  manifest_context.signing_key_e
      = az_span_create(reusable_scratch_space_head, jwsSIGNING_KEY_E_SIZE);
  reusable_scratch_space_head += jwsSIGNING_KEY_E_SIZE;

  result = base64_decode_signing_key(&manifest_context);

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] base64_decode_signing_key failed");
    return result;
  }

  /*------------------- Verify that the signature was signed by signing key
   * ------------------------*/

  if (!az_span_is_content_equal(manifest_context.alg_span, jws_alg_rs256))
  {
    Logger.Error("[JWS] Algorithm not supported");
    return AZ_ERROR_NOT_SUPPORTED;
  }

  result = jws_rs256_verify(
      az_span_create(
          az_span_ptr(manifest_context.base64_encoded_header),
          az_span_size(manifest_context.base64_encoded_header)
              + az_span_size(manifest_context.base64_encoded_payload) + 1),
      manifest_context.jws_signature,
      manifest_context.signing_key_n,
      manifest_context.signing_key_e,
      manifest_context.scratch_calculation_buffer);

  if (result != AZ_OK)
  {
    Logger.Error("[JWS] Verification of signed manifest SHA failed");
    return result;
  }

  /*------------------- Verify that the SHAs match ------------------------*/

  manifest_context.manifest_sha_calculation
      = az_span_create(reusable_scratch_space_head, jwsSHA256_SIZE);
  reusable_scratch_space_head += jwsSHA256_SIZE;
  manifest_context.parsed_manifest_sha
      = az_span_create(reusable_scratch_space_head, jwsSHA256_SIZE);
  reusable_scratch_space_head += jwsSHA256_SIZE;

  return verify_sha_match(&manifest_context, manifest_span);
}
