/* Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License. */

#include "AzIoTCryptoMbedTLS.h"

/* mbed TLS includes. */
#include "mbedtls/md.h"
#include "mbedtls/threading.h"

/*-----------------------------------------------------------*/

int AzIoTCryptoMbedTLS::HMAC256(
    const uint8_t* pucKey,
    int ulKeyLength,
    const uint8_t* pucData,
    int ulDataLength,
    uint8_t* pucOutput,
    int ulOutputLength,
    int* pulBytesCopied)
{
  int ulRet;
  mbedtls_md_context_t xCtx;
  mbedtls_md_type_t xMDType = MBEDTLS_MD_SHA256;

  if (ulOutputLength < 32)
  {
    return 1;
  }

  mbedtls_md_init(&xCtx);

  if (mbedtls_md_setup(&xCtx, mbedtls_md_info_from_type(xMDType), 1)
      || mbedtls_md_hmac_starts(&xCtx, pucKey, ulKeyLength)
      || mbedtls_md_hmac_update(&xCtx, pucData, ulDataLength)
      || mbedtls_md_hmac_finish(&xCtx, pucOutput))
  {
    ulRet = 1;
  }
  else
  {
    ulRet = 0;
    *pulBytesCopied = 32;
  }

  mbedtls_md_free(&xCtx);

  return ulRet;
}
/*-----------------------------------------------------------*/
