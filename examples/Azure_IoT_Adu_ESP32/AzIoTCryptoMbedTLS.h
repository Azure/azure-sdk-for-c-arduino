/* Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */

#ifndef AZIOTCRYPTOMBEDTLS_H
#define AZIOTCRYPTOMBEDTLS_H

#include <stdint.h>

namespace AzIoTCryptoMbedTLS
{
/**
 * @brief Compute HMAC SHA256
 *
 * @param[in] pucKey Pointer to key.
 * @param[in] ulKeyLength Length of Key.
 * @param[in] pucData Pointer to data for HMAC
 * @param[in] ulDataLength Length of data.
 * @param[in,out] pucOutput Buffer to place computed HMAC.
 * @param[out] ulOutputLength Length of output buffer.
 * @param[in] pulBytesCopied Number of bytes copied to out buffer.
 * @return An #uint32_t with result of operation.
 */
int HMAC256(
    const uint8_t* pucKey,
    int ulKeyLength,
    const uint8_t* pucData,
    int ulDataLength,
    uint8_t* pucOutput,
    int ulOutputLength,
    int* pulBytesCopied);
}; // namespace AzIoTCryptoMbedTLS

#endif // AZIOTCRYPTOMBEDTLS_H
