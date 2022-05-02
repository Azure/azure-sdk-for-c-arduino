// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// Wifi
#define IOT_CONFIG_WIFI_SSID ""
#define IOT_CONFIG_WIFI_PASSWORD ""

// Azure IoT
#define IOT_CONFIG_IOTHUB_FQDN "[HubName].azure-devices.net"
#define IOT_CONFIG_DEVICE_ID ""
#define IOT_CONFIG_DEVICE_KEY ""

//Azure IoT Edge - only use if you are connecting to an IoT Edge Gateway
//#define IOT_EDGE_GATEWAY "hostnameofgateway"

// Publish 1 message every 2 seconds
#define TELEMETRY_FREQUENCY_MILLISECS 2000
