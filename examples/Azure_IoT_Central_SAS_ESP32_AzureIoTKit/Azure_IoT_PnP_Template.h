// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#ifndef AZURE_IOT_PNP_TEMPLATE_H
#define AZURE_IOT_PNP_TEMPLATE_H

#include "AzureIoT.h"

void azure_pnp_init();

const az_span azure_pnp_get_model_id();

void azure_pnp_set_telemetry_frequency(size_t frequency_in_seconds);

int azure_pnp_send_telemetry(azure_iot_t* azure_iot);

#endif // AZURE_IOT_PNP_TEMPLATE_H
