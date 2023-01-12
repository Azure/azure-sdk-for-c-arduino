// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// Wifi
#define IOT_CONFIG_WIFI_SSID              "SSID"
#define IOT_CONFIG_WIFI_PASSWORD          "PWD"

// Azure IoT Central
#define IOT_CONFIG_DPS_ID_SCOPE           "ID Scope"
#define IOT_CONFIG_DEVICE_ID              "Device ID"
#define IOT_CONFIG_DEVICE_KEY             "Device Key"

// User-agent (url-encoded) provided by the MQTT client to Azure IoT Services.
// When developing for your own Arduino-based platform, please follow the format '(ard;<platform>)'.
#define IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;nanorp2040connect)"

// For how long the MQTT password (SAS token) is valid, in minutes.
// After that, the sample automatically generates a new password and re-connects.
#define IOT_CONFIG_MQTT_PASSWORD_LIFETIME_IN_MINUTES 60

// Time Zone Offset
#define IOT_CONFIG_TIME_ZONE -8
#define IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1
#define IOT_CONFIG_DAYLIGHT_SAVINGS true
