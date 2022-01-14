// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * Azure_IoT_PnP_Template.cpp implements the the Azure Plug-and-Play template
 * specific for the Espressif ESP32 Azure IoT Kit board.
 */
 
#ifndef AZURE_IOT_PNP_TEMPLATE_H
#define AZURE_IOT_PNP_TEMPLATE_H

#include "AzureIoT.h"

void azure_pnp_init();

const az_span azure_pnp_get_model_id();

/*
 * @brief     Sends the device description to Azure IoT Central.
 * @remark    Azure IoT Central expects the application to send a description of device and its capabilities.
 *            This function generates a description of the Espressif ESP32 Azure IoT Kit and sends it to
 *            Azure IoT Central.
 *
 * @param[in] azure_iot     A pointer the azure_iot_t instance with the state of the Azure IoT client.
 * @param[in] request_id    An unique identification number to correlate the response with when
 *                          `on_properties_update_completed` (set in azure_iot_config_t) is invoked.
 * @return int              0 if the function succeeds, non-zero if any error occurs.
 */
int azure_pnp_send_device_info(azure_iot_t* azure_iot, uint32_t request_id);

void azure_pnp_set_telemetry_frequency(size_t frequency_in_seconds);

int azure_pnp_send_telemetry(azure_iot_t* azure_iot);

int azure_pnp_update_properties(azure_iot_t* azure_iot);

int azure_pnp_handle_command_request(azure_iot_t* azure_iot, command_request_t command_request);

int azure_pnp_handle_properties_update(azure_iot_t* azure_iot, az_span properties, uint32_t request_id);

#endif // AZURE_IOT_PNP_TEMPLATE_H
