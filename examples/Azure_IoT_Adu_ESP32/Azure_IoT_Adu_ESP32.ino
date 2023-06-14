// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting,
 * subscribing and publishing to specific topics to use the messaging features
 * of the hub. Our azure-sdk-for-c is an MQTT client support library, helping
 * composing and parsing the MQTT topic names and messages exchanged with the
 * Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our
 * azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which
 * also handle the tcp connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens
 * for client authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the
 * `iot_configs.h` file.
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SampleAduJWS.h"
#include "SerialLogger.h"
#include "iot_configs.h"

#include "esp_ota_ops.h"
#include "esp_system.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2f" AZ_SDK_VERSION_STRING "(ard;esp32)"

#define ADU_UPDATE_ID                                                     \
  "{\"provider\":\"" ADU_UPDATE_PROVIDER "\",\"name\":\"" ADU_UPDATE_NAME \
  "\",\"version\":\"" ADU_DEVICE_VERSION "\"}"

#define SAMPLE_MQTT_TOPIC_LENGTH 128
#define SAMPLE_MQTT_PAYLOAD_LENGTH 1024

// ADU Values
#define ADU_PPV_DTMI AZ_IOT_ADU_CLIENT_AGENT_MODEL_ID
#define ADU_DEVICE_SHA_SIZE 32
#define ADU_SHA_PARTITION_READ_BUFFER_SIZE 32
#define HTTP_DOWNLOAD_CHUNK 4096

// ADU Feature Values
static az_iot_adu_client adu_client;
static az_iot_adu_client_update_request adu_update_request;
static az_iot_adu_client_update_manifest adu_update_manifest;
static char adu_new_version[16];
static bool process_update_request = false;
static bool send_init_state = true;
static bool did_update = false;
static char adu_scratch_buffer[10000];
static char adu_manifest_unescape_buffer[2000];
static char adu_verification_buffer[jwsSCRATCH_BUFFER_SIZE];
static char adu_sha_buffer[ADU_DEVICE_SHA_SIZE];
static char adu_calculated_sha_buffer[ADU_DEVICE_SHA_SIZE];
static char partition_read_buffer[ADU_SHA_PARTITION_READ_BUFFER_SIZE];
static int chunked_data_index;

static az_span pnp_components[] = { AZ_SPAN_FROM_STR(AZ_IOT_ADU_CLIENT_PROPERTIES_COMPONENT_NAME) };
static az_iot_adu_client_device_properties adu_device_information;

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client hub_client;
static HTTPClient http_client;
bool didNewlyConnect = true;

// MQTT Connection Values
static uint16_t connection_request_id = 0;
static char connection_request_id_buffer[16];
static char mqtt_client_id[256];
static char mqtt_username[256];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;

static char incoming_topic[SAMPLE_MQTT_TOPIC_LENGTH];
static char incoming_data[SAMPLE_MQTT_PAYLOAD_LENGTH];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &hub_client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

/* Azure Device Update root keys used to verify the signed deployment */
/* ADU.200702.R */
static uint8_t ucAzureIoTADURootKeyId200702[13] = "ADU.200702.R";
static uint8_t ucAzureIoTADURootKeyN200702[385]
    = { 0x00, 0xd5, 0x42, 0x2e, 0xaf, 0x11, 0x54, 0xa3, 0x50, 0x65, 0x87, 0xa2, 0x4d, 0x5b, 0xba,
        0x1a, 0xfb, 0xa9, 0x32, 0xdf, 0xe9, 0x99, 0x5f, 0x05, 0x45, 0xc8, 0xaf, 0xbd, 0x35, 0x1d,
        0x89, 0xe8, 0x27, 0x27, 0x58, 0xa3, 0xa8, 0xee, 0xc5, 0xc5, 0x1e, 0x4f, 0xf7, 0x92, 0xa6,
        0x12, 0x06, 0x7d, 0x3d, 0x7d, 0xb0, 0x07, 0xf6, 0x2c, 0x7f, 0xde, 0x6d, 0x2a, 0xf5, 0xbc,
        0x49, 0xbc, 0x15, 0xef, 0xf0, 0x81, 0xcb, 0x3f, 0x88, 0x4f, 0x27, 0x1d, 0x88, 0x71, 0x28,
        0x60, 0x08, 0xb6, 0x19, 0xd2, 0xd2, 0x39, 0xd0, 0x05, 0x1f, 0x3c, 0x76, 0x86, 0x71, 0xbb,
        0x59, 0x58, 0xbc, 0xb1, 0x88, 0x7b, 0xab, 0x56, 0x28, 0xbf, 0x31, 0x73, 0x44, 0x32, 0x10,
        0xfd, 0x3d, 0xd3, 0x96, 0x5c, 0xff, 0x4e, 0x5c, 0xb3, 0x6b, 0xff, 0x8b, 0x84, 0x9b, 0x8b,
        0x80, 0xb8, 0x49, 0xd0, 0x7d, 0xfa, 0xd6, 0x40, 0x58, 0x76, 0x4d, 0xc0, 0x72, 0x27, 0x75,
        0xcb, 0x9a, 0x2f, 0x9b, 0xb4, 0x9f, 0x0f, 0x25, 0xf1, 0x1c, 0xc5, 0x1b, 0x0b, 0x5a, 0x30,
        0x7d, 0x2f, 0xb8, 0xef, 0xa7, 0x26, 0x58, 0x53, 0xaf, 0xd5, 0x1d, 0x55, 0x01, 0x51, 0x0d,
        0xe9, 0x1b, 0xa2, 0x0f, 0x3f, 0xd7, 0xe9, 0x1d, 0x20, 0x41, 0xa6, 0xe6, 0x14, 0x0a, 0xae,
        0xfe, 0xf2, 0x1c, 0x2a, 0xd6, 0xe4, 0x04, 0x7b, 0xf6, 0x14, 0x7e, 0xec, 0x0f, 0x97, 0x83,
        0xfa, 0x58, 0xfa, 0x81, 0x36, 0x21, 0xb9, 0xa3, 0x2b, 0xfa, 0xd9, 0x61, 0x0b, 0x1a, 0x94,
        0xf7, 0xc1, 0xbe, 0x7f, 0x40, 0x14, 0x4a, 0xc9, 0xfa, 0x35, 0x7f, 0xef, 0x66, 0x70, 0x00,
        0xb1, 0xfd, 0xdb, 0xd7, 0x61, 0x0d, 0x3b, 0x58, 0x74, 0x67, 0x94, 0x89, 0x75, 0x76, 0x96,
        0x7c, 0x91, 0x87, 0xd2, 0x8e, 0x11, 0x97, 0xee, 0x7b, 0x87, 0x6c, 0x9a, 0x2f, 0x45, 0xd8,
        0x65, 0x3f, 0x52, 0x70, 0x98, 0x2a, 0xcb, 0xc8, 0x04, 0x63, 0xf5, 0xc9, 0x47, 0xcf, 0x70,
        0xf4, 0xed, 0x64, 0xa7, 0x74, 0xa5, 0x23, 0x8f, 0xb6, 0xed, 0xf7, 0x1c, 0xd3, 0xb0, 0x1c,
        0x64, 0x57, 0x12, 0x5a, 0xa9, 0x81, 0x84, 0x1f, 0xa0, 0xe7, 0x50, 0x19, 0x96, 0xb4, 0x82,
        0xb1, 0xac, 0x48, 0xe3, 0xe1, 0x32, 0x82, 0xcb, 0x40, 0x1f, 0xac, 0xc4, 0x59, 0xbc, 0x10,
        0x34, 0x51, 0x82, 0xf9, 0x28, 0x8d, 0xa8, 0x1e, 0x9b, 0xf5, 0x79, 0x45, 0x75, 0xb2, 0xdc,
        0x9a, 0x11, 0x43, 0x08, 0xbe, 0x61, 0xcc, 0x9a, 0xc4, 0xcb, 0x77, 0x36, 0xff, 0x83, 0xdd,
        0xa8, 0x71, 0x4f, 0x51, 0x8e, 0x0e, 0x7b, 0x4d, 0xfa, 0x79, 0x98, 0x8d, 0xbe, 0xfc, 0x82,
        0x7e, 0x40, 0x48, 0xa9, 0x12, 0x01, 0xa8, 0xd9, 0x7e, 0xf3, 0xa5, 0x1b, 0xf1, 0xfb, 0x90,
        0x77, 0x3e, 0x40, 0x87, 0x18, 0xc9, 0xab, 0xd9, 0xf7, 0x79 };
static uint8_t ucAzureIoTADURootKeyE200702[3] = { 0x01, 0x00, 0x01 };

/* ADU.200703.R */
static uint8_t ucAzureIoTADURootKeyId200703[13] = "ADU.200703.R";
static uint8_t ucAzureIoTADURootKeyN200703[385]
    = { 0x00, 0xb2, 0xa3, 0xb2, 0x74, 0x16, 0xfa, 0xbb, 0x20, 0xf9, 0x52, 0x76, 0xe6, 0x27, 0x3e,
        0x80, 0x41, 0xc6, 0xfe, 0xcf, 0x30, 0xf9, 0xc8, 0x96, 0xf5, 0x59, 0x0a, 0xaa, 0x81, 0xe7,
        0x51, 0x83, 0x8a, 0xc4, 0xf5, 0x17, 0x3a, 0x2f, 0x2a, 0xe6, 0x57, 0xd4, 0x71, 0xce, 0x8a,
        0x3d, 0xef, 0x9a, 0x55, 0x76, 0x3e, 0x99, 0xe2, 0xc2, 0xae, 0x4c, 0xee, 0x2d, 0xb8, 0x78,
        0xf5, 0xa2, 0x4e, 0x28, 0xf2, 0x9c, 0x4e, 0x39, 0x65, 0xbc, 0xec, 0xe4, 0x0d, 0xe5, 0xe3,
        0x38, 0xa8, 0x59, 0xab, 0x08, 0xa4, 0x1b, 0xb4, 0xf4, 0xa0, 0x52, 0xa3, 0x38, 0xb3, 0x46,
        0x21, 0x13, 0xcc, 0x3c, 0x68, 0x06, 0xde, 0xfe, 0x00, 0xa6, 0x92, 0x6e, 0xde, 0x4c, 0x47,
        0x10, 0xd6, 0x1c, 0x9c, 0x24, 0xf5, 0xcd, 0x70, 0xe1, 0xf5, 0x6a, 0x7c, 0x68, 0x13, 0x1d,
        0xe1, 0xc5, 0xf6, 0xa8, 0x4f, 0x21, 0x9f, 0x86, 0x7c, 0x44, 0xc5, 0x8a, 0x99, 0x1c, 0xc5,
        0xd3, 0x06, 0x9b, 0x5a, 0x71, 0x9d, 0x09, 0x1c, 0xc3, 0x64, 0x31, 0x6a, 0xc5, 0x17, 0x95,
        0x1d, 0x5d, 0x2a, 0xf1, 0x55, 0xc7, 0x66, 0xd4, 0xe8, 0xf5, 0xd9, 0xa9, 0x5b, 0x8c, 0xa2,
        0x6c, 0x62, 0x60, 0x05, 0x37, 0xd7, 0x32, 0xb0, 0x73, 0xcb, 0xf7, 0x4b, 0x36, 0x27, 0x24,
        0x21, 0x8c, 0x38, 0x0a, 0xb8, 0x18, 0xfe, 0xf5, 0x15, 0x60, 0x35, 0x8b, 0x35, 0xef, 0x1e,
        0x0f, 0x88, 0xa6, 0x13, 0x8d, 0x7b, 0x7d, 0xef, 0xb3, 0xe7, 0xb0, 0xc9, 0xa6, 0x1c, 0x70,
        0x7b, 0xcc, 0xf2, 0x29, 0x8b, 0x87, 0xf7, 0xbd, 0x9d, 0xb6, 0x88, 0x6f, 0xac, 0x73, 0xff,
        0x72, 0xf2, 0xef, 0x48, 0x27, 0x96, 0x72, 0x86, 0x06, 0xa2, 0x5c, 0xe3, 0x7d, 0xce, 0xb0,
        0x9e, 0xe5, 0xc2, 0xd9, 0x4e, 0xc4, 0xf3, 0x7f, 0x78, 0x07, 0x4b, 0x65, 0x88, 0x45, 0x0c,
        0x11, 0xe5, 0x96, 0x56, 0x34, 0x88, 0x2d, 0x16, 0x0e, 0x59, 0x42, 0xd2, 0xf7, 0xd9, 0xed,
        0x1d, 0xed, 0xc9, 0x37, 0x77, 0x44, 0x7e, 0xe3, 0x84, 0x36, 0x9f, 0x58, 0x13, 0xef, 0x6f,
        0xe4, 0xc3, 0x44, 0xd4, 0x77, 0x06, 0x8a, 0xcf, 0x5b, 0xc8, 0x80, 0x1c, 0xa2, 0x98, 0x65,
        0x0b, 0x35, 0xdc, 0x73, 0xc8, 0x69, 0xd0, 0x5e, 0xe8, 0x25, 0x43, 0x9e, 0xf6, 0xd8, 0xab,
        0x05, 0xaf, 0x51, 0x29, 0x23, 0x55, 0x40, 0x58, 0x10, 0xea, 0xb8, 0xe2, 0xcd, 0x5d, 0x79,
        0xcc, 0xec, 0xdf, 0xb4, 0x5b, 0x98, 0xc7, 0xfa, 0xe3, 0xd2, 0x6c, 0x26, 0xce, 0x2e, 0x2c,
        0x56, 0xe0, 0xcf, 0x8d, 0xee, 0xfd, 0x93, 0x12, 0x2f, 0x00, 0x49, 0x8d, 0x1c, 0x82, 0x38,
        0x56, 0xa6, 0x5d, 0x79, 0x44, 0x4a, 0x1a, 0xf3, 0xdc, 0x16, 0x10, 0xb3, 0xc1, 0x2d, 0x27,
        0x11, 0xfe, 0x1b, 0x98, 0x05, 0xe4, 0xa3, 0x60, 0x31, 0x99 };
static uint8_t ucAzureIoTADURootKeyE200703[3] = { 0x01, 0x00, 0x01 };

static SampleJWS::RootKey xADURootKeys[]
    = { { // Minus one on id to not count NULL
          .root_key_id
          = az_span_create(ucAzureIoTADURootKeyId200703, sizeof(ucAzureIoTADURootKeyId200703) - 1),
          .root_key_n = AZ_SPAN_FROM_BUFFER(ucAzureIoTADURootKeyN200703),
          .root_key_exponent = AZ_SPAN_FROM_BUFFER(ucAzureIoTADURootKeyE200703) },
        { // Minus one on id to not count NULL
          .root_key_id
          = az_span_create(ucAzureIoTADURootKeyId200702, sizeof(ucAzureIoTADURootKeyId200702) - 1),
          .root_key_n = AZ_SPAN_FROM_BUFFER(ucAzureIoTADURootKeyN200702),
          .root_key_exponent = AZ_SPAN_FROM_BUFFER(ucAzureIoTADURootKeyE200702) } };

static void connect_to_wifi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initialize_time()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

// get_request_id sets a request Id into connection_request_id_buffer and
// monotonically increases the counter for the next MQTT operation.
static az_span get_request_id(void)
{
  az_span remainder;
  az_span out_span = az_span_create(
      (uint8_t*)connection_request_id_buffer, sizeof(connection_request_id_buffer));

  connection_request_id++;
  if (connection_request_id == UINT16_MAX)
  {
    // Connection id has looped.  Reset.
    connection_request_id = 1;
  }

  az_result rc = az_span_u32toa(out_span, connection_request_id, &remainder);
  if (az_result_failed(rc))
  {
    Logger.Info("Failed to get request id");
  }

  return az_span_slice(out_span, 0, az_span_size(out_span) - az_span_size(remainder));
}

static void prvParseAduUrl(az_span xUrl, az_span* pxHost, az_span* pxPath)
{
  xUrl = az_span_slice_to_end(xUrl, sizeof("http://") - 1);
  int32_t lPathPosition = az_span_find(xUrl, AZ_SPAN_FROM_STR("/"));
  *pxHost = az_span_slice(xUrl, 0, lPathPosition);
  *pxPath = az_span_slice_to_end(xUrl, lPathPosition);
}

void update_started() { Serial.println("CALLBACK:  HTTP update process started"); }

void update_finished() { Serial.println("CALLBACK:  HTTP update process finished"); }

void update_progress(int cur, int total)
{
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) { Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err); }

static az_result download_and_write_to_flash(void)
{
  az_span url_host_span;
  az_span url_path_span;
  prvParseAduUrl(adu_update_request.file_urls[0].url, &url_host_span, &url_path_span);

  WiFiClient client;

  httpUpdate.onStart(update_started);
  httpUpdate.onEnd(update_finished);
  httpUpdate.onProgress(update_progress);
  httpUpdate.onError(update_error);
  httpUpdate.rebootOnUpdate(false);

  char null_terminated_host[128];
  (void)memcpy(null_terminated_host, az_span_ptr(url_host_span), az_span_size(url_host_span));
  null_terminated_host[az_span_size(url_host_span)] = '\0';

  char null_terminated_path[128];
  (void)memcpy(null_terminated_path, az_span_ptr(url_path_span), az_span_size(url_path_span));
  null_terminated_path[az_span_size(url_path_span)] = '\0';

  t_httpUpdate_return ret
      = httpUpdate.update(client, null_terminated_host, 80, null_terminated_path);

  if (ret != HTTP_UPDATE_OK)
  {
    Logger.Error("Image download failed");
    return AZ_ERROR_CANCELED;
  }
  else
  {
    Logger.Info("Download of image succeeded");
    return AZ_OK;
  }
}

static az_result verify_image(az_span sha256_hash, int32_t update_size)
{
  az_result result;
  esp_err_t espErr;

  int32_t out_size;
  int32_t read_size;
  az_span out_buffer = AZ_SPAN_FROM_BUFFER(adu_sha_buffer);

  Logger.Info("Verifying downloaded image with manifest SHA256 hash");

  const esp_partition_t* current_partition = esp_ota_get_running_partition();
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(current_partition);

  if (az_result_failed(result = az_base64_decode(out_buffer, sha256_hash, &out_size)))
  {
    Logger.Error("az_base64_decode failed: core error=0x" + String(result, HEX));
    return result;
  }
  else
  {
    Logger.Info("Unencoded the base64 encoding\r\n");
    result = AZ_OK;
  }

  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
  mbedtls_md_starts(&ctx);

  Logger.Info("Starting the mbedtls calculation: image size " + String(update_size));
  for (size_t offset_index = 0; offset_index < update_size;
       offset_index += ADU_SHA_PARTITION_READ_BUFFER_SIZE)
  {
    read_size = update_size - offset_index < ADU_SHA_PARTITION_READ_BUFFER_SIZE
        ? update_size - offset_index
        : ADU_SHA_PARTITION_READ_BUFFER_SIZE;

    espErr
        = esp_partition_read_raw(update_partition, offset_index, partition_read_buffer, read_size);
    (void)espErr;

    mbedtls_md_update(&ctx, (const unsigned char*)partition_read_buffer, read_size);
  }

  printf("\r\n");

  Logger.Info("Done\r\n");

  mbedtls_md_finish(&ctx, (unsigned char*)adu_calculated_sha_buffer);
  mbedtls_md_free(&ctx);

  if (memcmp(adu_sha_buffer, adu_calculated_sha_buffer, ADU_DEVICE_SHA_SIZE) == 0)
  {
    Logger.Info("SHAs match\r\n");
    result = AZ_OK;
  }
  else
  {
    Logger.Info("SHAs do not match\r\n");
    result = AZ_ERROR_ITEM_NOT_FOUND;
  }

  return result;
}

// request_all_properties sends a request to Azure IoT Hub to request all
// properties for the device.  This call does not block.  Properties will be
// received on a topic previously subscribed to
// (AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC.)
static void request_all_properties(void)
{
  az_result rc;

  Logger.Info("Client requesting device property document from service.");

  // Get the topic to publish the property document request.
  char property_document_topic_buffer[SAMPLE_MQTT_TOPIC_LENGTH];
  rc = az_iot_hub_client_properties_document_get_publish_topic(
      &hub_client,
      get_request_id(),
      property_document_topic_buffer,
      sizeof(property_document_topic_buffer),
      NULL);
  if (az_result_failed(rc))
  {
    Logger.Info("Failed to get the property document topic");
  }

  if (esp_mqtt_client_publish(
          mqtt_client, property_document_topic_buffer, NULL, 0, MQTT_QOS1, DO_NOT_RETAIN_MSG)
      < 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

// send_adu_device_reported_property writes a property payload reporting device
// state and then sends it to Azure IoT Hub.
static void send_adu_device_information_property(
    az_iot_adu_client_agent_state agent_state,
    az_iot_adu_client_workflow* workflow)
{
  az_result rc;

  // Get the property topic to send a reported property update.
  char property_update_topic_buffer[SAMPLE_MQTT_TOPIC_LENGTH];
  rc = az_iot_hub_client_properties_get_reported_publish_topic(
      &hub_client,
      get_request_id(),
      property_update_topic_buffer,
      sizeof(property_update_topic_buffer),
      NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Failed to get the property update topic");
  }

  // Write the updated reported property message.
  char reported_property_payload_buffer[SAMPLE_MQTT_PAYLOAD_LENGTH] = { 0 };
  az_span reported_property_payload = AZ_SPAN_FROM_BUFFER(reported_property_payload_buffer);
  az_json_writer adu_payload;

  rc = az_json_writer_init(&adu_payload, reported_property_payload, NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Failed to get the adu device information payload");
  }

  rc = az_iot_adu_client_get_agent_state_payload(
      &adu_client, &adu_device_information, agent_state, workflow, NULL, &adu_payload);
  if (az_result_failed(rc))
  {
    Logger.Error("Failed to get the adu device information payload");
  }

  reported_property_payload = az_json_writer_get_bytes_used_in_destination(&adu_payload);

  Logger.Info("Topic " + String(property_update_topic_buffer));
  Logger.Info("Payload " + String(reported_property_payload_buffer));

  if (esp_mqtt_client_publish(
          mqtt_client,
          property_update_topic_buffer,
          (const char*)az_span_ptr(reported_property_payload),
          az_span_size(reported_property_payload),
          0,
          DO_NOT_RETAIN_MSG)
      < 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Client published the device's information.");
  }
}

static void send_adu_accept_manifest_property(
    int32_t version_number,
    az_iot_adu_client_request_decision response_code)
{
  az_result rc;

  // Get the topic to publish the property document request.
  char property_accept_topic_buffer[SAMPLE_MQTT_TOPIC_LENGTH];
  rc = az_iot_hub_client_properties_get_reported_publish_topic(
      &hub_client,
      get_request_id(),
      property_accept_topic_buffer,
      sizeof(property_accept_topic_buffer),
      NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Could not get properties topic");
    return;
  }

  char property_payload_buffer[SAMPLE_MQTT_PAYLOAD_LENGTH];
  az_span property_buffer = AZ_SPAN_FROM_BUFFER(property_payload_buffer);

  az_json_writer adu_payload;

  rc = az_json_writer_init(&adu_payload, property_buffer, NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Failed to get the adu device information payload");
  }

  rc = az_iot_adu_client_get_service_properties_response(
      &adu_client, version_number, response_code, &adu_payload);
  if (az_result_failed(rc))
  {
    Logger.Error("Could not get service properties response");
    return;
  }

  if (esp_mqtt_client_publish(
          mqtt_client,
          property_accept_topic_buffer,
          (const char*)az_span_ptr(property_buffer),
          az_span_size(property_buffer),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      < 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

static bool is_update_too_big(int64_t update_size)
{
  if (update_size < 0)
  {
    return true;
  }

  const esp_partition_t* current_partition = esp_ota_get_running_partition();
  if (current_partition == NULL)
  {
    Logger.Error("esp_ota_get_running_partition failed");
    return true;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(current_partition);
  if (update_partition == NULL)
  {
    Logger.Error("esp_ota_get_next_update_partition failed");
    return true;
  }

  /* size of the next ota partition to be written to */
  return (update_partition->size < update_size);
}

static bool is_update_already_applied(void)
{
  return (
      (az_span_size(adu_update_manifest.instructions.steps[0].handler_properties.installed_criteria)
       == sizeof(ADU_DEVICE_VERSION) - 1)
      && strncmp(
             ADU_DEVICE_VERSION,
             (char*)az_span_ptr(
                 adu_update_manifest.instructions.steps[0].handler_properties.installed_criteria),
             az_span_size(
                 adu_update_manifest.instructions.steps[0].handler_properties.installed_criteria))
          == 0);
}

// process_device_property_message handles incoming properties from Azure IoT
// Hub.
static void process_device_property_message(
    az_span message_span,
    az_iot_hub_client_properties_message_type message_type)
{
  az_json_reader jr;
  az_json_reader jr_adu_manifest;
  int32_t out_manifest_size;
  az_result rc = az_json_reader_init(&jr, message_span, NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Could not initialize json reader");
    return;
  }

  int32_t version_number;
  rc = az_iot_hub_client_properties_get_properties_version(
      &hub_client, &jr, message_type, &version_number);
  if (az_result_failed(rc))
  {
    Logger.Error("Could not get property version");
    return;
  }

  rc = az_json_reader_init(&jr, message_span, NULL);
  if (az_result_failed(rc))
  {
    Logger.Error("Could not initialize json reader");
    return;
  }

  az_span component_name;

  az_span scratch_buffer_span
      = az_span_create((uint8_t*)adu_scratch_buffer, (int32_t)sizeof(adu_scratch_buffer));

  // Applications call az_iot_hub_client_properties_get_next_component_property
  // to enumerate properties received.
  while (az_result_succeeded(az_iot_hub_client_properties_get_next_component_property(
      &hub_client, &jr, message_type, AZ_IOT_HUB_CLIENT_PROPERTY_WRITABLE, &component_name)))
  {
    if (az_iot_adu_client_is_component_device_update(&adu_client, component_name))
    {
      // ADU Component
      rc = az_iot_adu_client_parse_service_properties(&adu_client, &jr, &adu_update_request);

      if (az_result_failed(rc))
      {
        Logger.Error("az_iot_adu_client_parse_service_properties failed" + String(rc));
        return;
      }
      else
      {
        if (adu_update_request.workflow.action == AZ_IOT_ADU_CLIENT_SERVICE_ACTION_APPLY_DEPLOYMENT)
        {
          adu_update_request.update_manifest = az_json_string_unescape(
              adu_update_request.update_manifest, adu_update_request.update_manifest);

          rc = az_json_reader_init(&jr_adu_manifest, adu_update_request.update_manifest, NULL);

          if (az_result_failed(rc))
          {
            Logger.Error("az_iot_adu_client_parse_update_manifest failed" + String(rc));
            return;
          }

          rc = az_iot_adu_client_parse_update_manifest(
              &adu_client, &jr_adu_manifest, &adu_update_manifest);

          if (az_result_failed(rc))
          {
            Logger.Error("az_iot_adu_client_parse_update_manifest failed" + String(rc));
            return;
          }

          Logger.Info("Parsed Azure device update manifest.");

          rc = SampleJWS::ManifestAuthenticate(
              adu_update_request.update_manifest,
              adu_update_request.update_manifest_signature,
              &xADURootKeys[0],
              sizeof(xADURootKeys) / sizeof(xADURootKeys[0]),
              AZ_SPAN_FROM_BUFFER(adu_verification_buffer));
          if (az_result_failed(rc))
          {
            Logger.Error("ManifestAuthenticate failed " + String(rc));
            process_update_request = false;
            return;
          }

          Logger.Info("Manifest authenticated successfully");

          if (is_update_already_applied())
          {
            Logger.Info("Update already applied");
            send_adu_accept_manifest_property(
                version_number, AZ_IOT_ADU_CLIENT_REQUEST_DECISION_REJECT);
            process_update_request = false;
          }
          else if (is_update_too_big(adu_update_manifest.files[0].size_in_bytes))
          {
            Logger.Info("Image size larger than flash bank size");
            send_adu_accept_manifest_property(
                version_number, AZ_IOT_ADU_CLIENT_REQUEST_DECISION_REJECT);
            process_update_request = false;
          }
          else
          {
            Logger.Info("Sending manifest property accept");
            send_adu_accept_manifest_property(
                version_number, AZ_IOT_ADU_CLIENT_REQUEST_DECISION_ACCEPT);

            process_update_request = true;
          }
        }
        else if (adu_update_request.workflow.action == AZ_IOT_ADU_CLIENT_SERVICE_ACTION_CANCEL)
        {
          Logger.Info("ADU action received: cancelled deployment");
          send_adu_device_information_property(AZ_IOT_ADU_CLIENT_AGENT_STATE_IDLE, NULL);
          process_update_request = false;
        }
        else
        {
          Logger.Error(
              "Unknown workflow action received: " + String(adu_update_request.workflow.action));

          send_adu_device_information_property(
              AZ_IOT_ADU_CLIENT_AGENT_STATE_FAILED, &adu_update_request.workflow);

          process_update_request = false;
        }
      }
    }
    else
    {
      Logger.Info("Unknown Property Received");
      // The JSON reader must be advanced regardless of whether the property
      // is of interest or not.
      rc = az_json_reader_next_token(&jr);
      if (az_result_failed(rc))
      {
        Logger.Error("Invalid JSON. Could not move to next property value");
      }

      // Skip children in case the property value is an object
      rc = az_json_reader_skip_children(&jr);
      if (az_result_failed(rc))
      {
        Logger.Error("Invalid JSON. Could not skip children");
      }

      rc = az_json_reader_next_token(&jr);
      if (az_result_failed(rc))
      {
        Logger.Error("Invalid JSON. Could not move to next property name");
      }
    }
  }
}

// handle_device_property_message handles incoming properties from Azure IoT
// Hub.
static void handle_device_property_message(
    byte* payload,
    unsigned int length,
    az_iot_hub_client_properties_message const* property_message)
{
  az_span const message_span = az_span_create((uint8_t*)payload, length);

  // Invoke appropriate action per message type (3 types only).
  switch (property_message->message_type)
  {
    // A message from a property GET publish message with the property document as
    // a payload.
    case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_GET_RESPONSE:
      Logger.Info("Message Type: GET");
      process_device_property_message(message_span, property_message->message_type);
      break;

    // An update to the desired properties with the properties as a payload.
    case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_WRITABLE_UPDATED:
      Logger.Info("Message Type: Desired Properties");
      process_device_property_message(message_span, property_message->message_type);
      break;

    // When the device publishes a property update, this message type arrives when
    // server acknowledges this.
    case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_ACKNOWLEDGEMENT:
      Logger.Info("Message Type: IoT Hub has acknowledged properties that the "
                  "device sent");
      break;

    // An error has occurred
    case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_ERROR:
      Logger.Error("Message Type: Request Error");
      break;
  }
}

void received_callback(char* topic, byte* payload, unsigned int length)
{
  az_result rc;

  az_iot_hub_client_properties_message property_message;

  az_span topic_span = az_span_create((uint8_t*)topic, (int32_t)strlen(topic));

  rc = az_iot_hub_client_properties_parse_received_topic(
      &hub_client, topic_span, &property_message);
  if (az_result_succeeded(rc))
  {
    Logger.Info("Client received a properties topic.");
    Logger.Info("Status: " + String(property_message.status));

    handle_device_property_message(payload, length, &property_message);
  }
  else
  {
    Logger.Error(
        "Error: " + String(rc) + " Received a message from an unknown topic: " + String(topic));
  }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      Logger.Info("ESP ERROR " + String(event->error_handle->esp_tls_last_esp_err));
      Logger.Info("TLS ERROR " + String(event->error_handle->esp_tls_stack_err));
      Logger.Info("ERROR TYPE " + String(event->error_handle->error_type));
      Logger.Info(
          "ESP Error " + String(esp_err_to_name(event->error_handle->esp_tls_last_esp_err)));
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
      }

      r = esp_mqtt_client_subscribe(
          mqtt_client, AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe to properties messages.");
      }
      else
      {
        Logger.Info("Subscribed to properties messages; message id:" + String(r));
      }

      r = esp_mqtt_client_subscribe(
          mqtt_client, AZ_IOT_HUB_CLIENT_PROPERTIES_WRITABLE_UPDATES_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe to writable property updates.");
      }
      else
      {
        Logger.Info("Subscribed to writable property updates; message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");

      // Chunked data will not have a topic. Only copy when topic length is greater
      // than 0.
      if (event->topic_len > 0)
      {
        for (i = 0; i < (SAMPLE_MQTT_TOPIC_LENGTH - 1) && i < event->topic_len; i++)
        {
          incoming_topic[i] = event->topic[i];
        }
        incoming_topic[i] = '\0';
        Logger.Info("Topic: " + String(incoming_topic));
      }

      for (i = 0; i < (SAMPLE_MQTT_PAYLOAD_LENGTH - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';

      if (event->total_data_len > event->data_len)
      {
        Logger.Info("Received Chunked Payload Data");
        chunked_data_index = event->current_data_offset != 0 ? chunked_data_index : 0;
        // This data is going to be incoming in chunks. Piece it together.
        memcpy((void*)&adu_scratch_buffer[chunked_data_index], event->data, event->data_len);
        chunked_data_index += event->data_len;

        if (chunked_data_index == event->total_data_len)
        {
          Logger.Info("Received all of the chunked data. Moving to process.");
          adu_scratch_buffer[chunked_data_index] = '\0';
          Logger.Info("Data: " + String(adu_scratch_buffer));
          received_callback(incoming_topic, (byte*)adu_scratch_buffer, event->total_data_len);
        }
      }
      else
      {
        received_callback(incoming_topic, (byte*)incoming_data, event->data_len);
      }

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

static void initialize_iot_hub_client()
{
  Logger.Info("------------------------------------------------------------------------------");
  Logger.Info("ADU SAMPLE");
  Logger.Info("Version: " + String(ADU_DEVICE_VERSION));
  Logger.Info("------------------------------------------------------------------------------");

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
  options.model_id = AZ_SPAN_FROM_STR(ADU_PPV_DTMI);
  options.component_names = pnp_components;
  options.component_names_length = sizeof(pnp_components) / sizeof(pnp_components[0]);

  if (az_result_failed(az_iot_hub_client_init(
          &hub_client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  if (az_result_failed(az_iot_adu_client_init(&adu_client, NULL)))
  {
    Logger.Error("Failed initializing Azure IoT Adu client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &hub_client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &hub_client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initialize_mqtt_client()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;
  mqtt_config.buffer_size = 2048;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t get_epoch_time_in_seconds() { return (uint32_t)time(NULL); }

static void establish_connection()
{
  connect_to_wifi();
  initialize_time();
  initialize_iot_hub_client();
  (void)initialize_mqtt_client();
}

static void get_telemetry_payload(az_span payload, az_span* out_payload)
{
  az_result rc;
  az_span original_payload = payload;

  payload = az_span_copy(payload, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
  rc = az_span_u32toa(payload, telemetry_send_count++, &payload);
  (void)rc;
  payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" }"));
  payload = az_span_copy_u8(payload, '\0');

  *out_payload = az_span_slice(
      original_payload, 0, az_span_size(original_payload) - az_span_size(payload) - 1);
}

static void send_telemetry()
{
  az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to
  // reflect the current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &hub_client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  get_telemetry_payload(telemetry, &telemetry);

  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char*)az_span_ptr(telemetry),
          az_span_size(telemetry),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      < 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

// Arduino setup and loop main functions.

void setup()
{
  adu_device_information = az_iot_adu_client_device_properties_default();
  adu_device_information.manufacturer = AZ_SPAN_FROM_STR(ADU_DEVICE_MANUFACTURER);
  adu_device_information.model = AZ_SPAN_FROM_STR(ADU_DEVICE_MODEL);
  adu_device_information.adu_version = AZ_SPAN_FROM_STR(AZ_IOT_ADU_CLIENT_AGENT_VERSION);
  adu_device_information.delivery_optimization_agent_version = AZ_SPAN_EMPTY;
  adu_device_information.update_id = AZ_SPAN_FROM_STR(ADU_UPDATE_ID);

  establish_connection();
}

void loop()
{
  az_result result;

  if (WiFi.status() != WL_CONNECTED)
  {
    Logger.Info("Connecting to WiFI");
    connect_to_wifi();
    send_init_state = true;
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initialize_mqtt_client();
    send_init_state = true;
  }
#endif
  else if (millis() > next_telemetry_send_time_ms)
  {
    if (send_init_state)
    {
      Logger.Info("Requesting all device properties");
      request_all_properties();
      Logger.Info("Sending ADU device information");
      send_adu_device_information_property(AZ_IOT_ADU_CLIENT_AGENT_STATE_IDLE, NULL);

      send_init_state = false;
    }
    send_telemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;

    if (process_update_request)
    {
      send_adu_device_information_property(
          AZ_IOT_ADU_CLIENT_AGENT_STATE_DEPLOYMENT_IN_PROGRESS, &adu_update_request.workflow);

      result = download_and_write_to_flash();

      if (result == AZ_OK)
      {
        if (adu_update_request.workflow.action == AZ_IOT_ADU_CLIENT_SERVICE_ACTION_CANCEL)
        {
          Logger.Info("Cancellation request was received during download. "
                      "Aborting update.");
          send_adu_device_information_property(AZ_IOT_ADU_CLIENT_AGENT_STATE_IDLE, NULL);
          process_update_request = false;
        }
        else if (
            adu_update_request.workflow.action == AZ_IOT_ADU_CLIENT_SERVICE_ACTION_APPLY_DEPLOYMENT)
        {
          result = verify_image(
              adu_update_manifest.files[0].hashes->hash_value,
              adu_update_manifest.files[0].size_in_bytes);

          // Clean shutdown of MQTT
          esp_mqtt_client_disconnect(mqtt_client);
          esp_mqtt_client_stop(mqtt_client);

          if (result == AZ_OK)
          {
            // All is verified. Reboot device to new update.
            esp_restart();
          }
        }
        else
        {
          Logger.Error(
              "Unknown workflow action received: " + String(adu_update_request.workflow.action));

          send_adu_device_information_property(
              AZ_IOT_ADU_CLIENT_AGENT_STATE_FAILED, &adu_update_request.workflow);

          process_update_request = false;
        }
      }
    }
  }
}