/* Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */

/**
 * @file
 *
 * @brief APIs to authenticate an ADU manifest.
 *
 */

#ifndef SAMPLEADUJWS_H
#define SAMPLEADUJWS_H

#include <stdint.h>

#include <az_core.h>

#define jwsPKCS7_PAYLOAD_OFFSET 19

#define jwsRSA3072_SIZE 384
#define jwsSHA256_SIZE 32
#define jwsJWS_HEADER_SIZE 1400
#define jwsJWS_PAYLOAD_SIZE 60
#define jwsJWK_HEADER_SIZE 48
#define jwsJWK_PAYLOAD_SIZE 700
#define jwsSIGNATURE_SIZE 400
#define jwsSIGNING_KEY_E_SIZE 10
#define jwsSIGNING_KEY_N_SIZE jwsRSA3072_SIZE
#define jwsSHA_CALCULATION_SCRATCH_SIZE jwsRSA3072_SIZE + jwsSHA256_SIZE

/* This is the minimum amount of space needed to store values which are held at
 * the same time. jwsJWS_PAYLOAD_SIZE, one jwsSIGNATURE_SIZE, and one
 * jwsSHA256_SIZE are excluded since they will reuse buffer space. */
#define jwsSCRATCH_BUFFER_SIZE                                                       \
  (jwsJWS_HEADER_SIZE + jwsJWK_HEADER_SIZE + jwsJWK_PAYLOAD_SIZE + jwsSIGNATURE_SIZE \
   + jwsSIGNING_KEY_N_SIZE + jwsSIGNING_KEY_E_SIZE + jwsSHA_CALCULATION_SCRATCH_SIZE)

namespace SampleJWS
{

/**
 * @brief Holds the values of the root key used to verify the JWS signature.
 */
typedef struct RootKey
{
  az_span root_key_id;
  az_span root_key_n;
  az_span root_key_exponent;
} RootKey;

/**
 * @brief Authenticate the manifest from ADU.
 *
 * @param[in] manifest_span The escaped manifest from the ADU twin property.
 * @param[in] jws_span The JWS used to authenticate \p manifest_span.
 * @param[in] root_keys An array of root keys that may be used to verify the payload.
 * @param[in] root_keys_length The number of root keys in \p root_keys.
 * @param[in] scratch_buffer_span Scratch buffer space for calculations. It
 * should be `jwsSCRATCH_BUFFER_SIZE` in length.
 * @return az_result The return value of this function.
 * @retval AZ_OK if successful.
 * @retval Otherwise if failed.
 */
az_result ManifestAuthenticate(
    az_span manifest_span,
    az_span jws_span,
    RootKey* root_keys,
    uint32_t root_keys_length,
    az_span scratch_buffer_span);
}; // namespace SampleJWS

#endif /* SAMPLEADUJWS_H */
