// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * @note You MUST NOT use any symbols (macros, functions, structures, enums, etc.)
 * prefixed with an underscore ('_') directly in your application code. These symbols
 * are part of Azure SDK's internal implementation; we do not document these symbols
 * and they are subject to change in future versions of the SDK which would break your code.
 */

// Maximum Number of Files Handled by this ADU Agent (per step)
// This must be no larger than #_AZ_IOT_ADU_CLIENT_MAX_TOTAL_FILE_COUNT.
#ifndef _AZ_IOT_ADU_CLIENT_MAX_FILE_COUNT_PER_STEP
#define _AZ_IOT_ADU_CLIENT_MAX_FILE_COUNT_PER_STEP 2
#endif // _AZ_IOT_ADU_CLIENT_MAX_FILE_COUNT_PER_STEP

// Maximum Number of Files Handled by this ADU Agent (total files for this deployment)
#ifndef _AZ_IOT_ADU_CLIENT_MAX_TOTAL_FILE_COUNT
#define _AZ_IOT_ADU_CLIENT_MAX_TOTAL_FILE_COUNT 2
#endif // _AZ_IOT_ADU_CLIENT_MAX_TOTAL_FILE_COUNT

// Maximum Number of Steps Handled by this ADU Agent
#ifndef _AZ_IOT_ADU_CLIENT_MAX_INSTRUCTIONS_STEPS
#define _AZ_IOT_ADU_CLIENT_MAX_INSTRUCTIONS_STEPS 2
#endif // _AZ_IOT_ADU_CLIENT_MAX_INSTRUCTIONS_STEPS

// Maximum Number of File Hashes Handled by this ADU Agent
#ifndef _AZ_IOT_ADU_CLIENT_MAX_FILE_HASH_COUNT
#define _AZ_IOT_ADU_CLIENT_MAX_FILE_HASH_COUNT 2
#endif // _AZ_IOT_ADU_CLIENT_MAX_FILE_HASH_COUNT

// Maximum Number of Custom Device Properties
#ifndef _AZ_IOT_ADU_CLIENT_MAX_DEVICE_CUSTOM_PROPERTIES
#define _AZ_IOT_ADU_CLIENT_MAX_DEVICE_CUSTOM_PROPERTIES 5
#endif // _AZ_IOT_ADU_CLIENT_MAX_DEVICE_CUSTOM_PROPERTIES