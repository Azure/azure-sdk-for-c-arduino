// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// Wifi
#define IOT_CONFIG_WIFI_SSID              "SSID"
#define IOT_CONFIG_WIFI_PASSWORD          "PWD"

// Define if using device x509 certificates
// #define IOT_CONFIG_USE_X509_CERT

#ifdef IOT_CONFIG_USE_X509_CERT

/*

Device certificate should be of the form:

"-----BEGIN CERTIFICATE-----\r\n" \
"MIIBJDCBywIUfeHrebBVa2eZAbouBgACp9R3BncwCgYIKoZIzj0EAwIwETEPMA0G\r\n" \
"A1UEAwwGRFBTIENBMB4XDTIyMDMyMjazMTAzN1oXDTIzMDMyMjIzMTAzN1owGTEX\r\n" \
"MBUGA1UEAwwOY29udG9zby1kZXZpY2UwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNC\r\n" \
.......
"YmGzdaHTb6P1W+p+jmc+jJn1MAoGCXqGSM49BAMCA0gAMEUCIEnbEMsAdGFroMwl\r\n" \
"vTfQahwsxN3xink9z1gtirrjQlqDAiEAyU+6TUJcG6d9JF+uJqsLFpsbbF3IzGAw\r\n" \
"yC+koNRC0MU=\r\n" \
"-----END CERTIFICATE-----"

*/
#define IOT_CONFIG_DEVICE_CERT "Device Certificate"

/*

Device certificate private key should be of the form:

"-----BEGIN EC PRIVATE KEY-----\r\n" \
"MHcCAQEEIKGXkMMiO9D7jYpUjUGTBn7gGzeKPeNzCP83wbfQfLd9obAoGCCqGSM49\r\n" \
"AwEHoUQDQgAEU6nQoYbjgJvBwaeD6MyAYmOSDg0QhEdyyV337qrlIbDEKvFsn1El\r\n" \
"yRabc4dNp2Jhs3Xh02+j9Vvqfo5nPoyZ9Q==\r\n" \
"-----END EC PRIVATE KEY-----"

Note the type of key may different in your case. Such as BEGIN PRIVATE KEY
or BEGIN RSA PRIVATE KEY.

*/
#define IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY "Device Certificate Private Key"

#endif // IOT_CONFIG_USE_X509_CERT

// Azure IoT
#define IOT_CONFIG_IOTHUB_FQDN "[your Azure IoT host name].azure-devices.net"
#define IOT_CONFIG_DEVICE_ID "Device ID"
// Use device key if not using certificates
#ifndef IOT_CONFIG_USE_X509_CERT
  #define IOT_CONFIG_DEVICE_KEY "Device Key"
#endif  // IOT_CONFIG_USE_X509_CERT

// Publish 1 message every 2 seconds
#define TELEMETRY_FREQUENCY_MILLISECS 2000
