// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 * 
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which also handle the tcp connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 * 
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h` file. 
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers 
#include "AzIoTSasToken.h"
#include "AzIoTCryptoMbedTLS.h"
#include "SampleAduJWS.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'. 
#define AZURE_SDK_CLIENT_USER_AGENT "c%2f" AZ_SDK_VERSION_STRING "(ard;esp32)"

#define SAMPLE_MQTT_TOPIC_LENGTH 128
#define SAMPLE_MQTT_PAYLOAD_LENGTH 1024

// ADU Values
#define ADU_DEVICE_MANUFACTURER "ESPRESSIF"
#define ADU_DEVICE_MODEL "ESP32-Embedded"
#define ADU_DEVICE_VERSION "1.1"
#define HTTP_DOWNLOAD_CHUNK 4096

// ADU Feature Values
static az_iot_adu_client adu_client;
static az_iot_adu_client_update_request xBaseUpdateRequest;
static az_iot_adu_client_update_manifest xBaseUpdateManifest;
static char adu_new_version[16];
static bool did_parse_update = false;
static bool did_update = false;
static char adu_scratch_buffer[10000];
static char adu_verification_buffer[ jwsSCRATCH_BUFFER_SIZE ];
static int chunked_data_index;

#define AZ_IOT_ADU_DTMI "dtmi:azure:iot:deviceUpdateModel;1"

static az_span pnp_components[] =
{
    AZ_SPAN_FROM_STR( AZ_IOT_ADU_CLIENT_PROPERTIES_COMPONENT_NAME )
};
az_iot_adu_client_device_properties adu_device_information = {
  .manufacturer = AZ_SPAN_LITERAL_FROM_STR(ADU_DEVICE_MANUFACTURER),
  .model = AZ_SPAN_LITERAL_FROM_STR(ADU_DEVICE_MODEL),
  .adu_version = AZ_SPAN_LITERAL_FROM_STR(AZ_IOT_ADU_CLIENT_AGENT_VERSION),
  .delivery_optimization_agent_version = AZ_SPAN_EMPTY,
  .update_id = {
    .provider = AZ_SPAN_LITERAL_FROM_STR(ADU_DEVICE_MANUFACTURER),
    .name = AZ_SPAN_LITERAL_FROM_STR(ADU_DEVICE_MODEL),
    .version = AZ_SPAN_LITERAL_FROM_STR(ADU_DEVICE_VERSION)
  }
};

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF   1

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

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
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

// get_request_id sets a request Id into connection_request_id_buffer and monotonically
// increases the counter for the next MQTT operation.
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
  if(az_result_failed(rc))
  {
    Logger.Info("Failed to get request id");
  }

  return az_span_slice(out_span, 0, az_span_size(out_span) - az_span_size(remainder));
}

static void prvParseAduUrl( az_span xUrl,
                            az_span * pxHost,
                            az_span * pxPath )
{
    xUrl = az_span_slice_to_end( xUrl, sizeof( "http://" ) - 1 );
    int32_t lPathPosition = az_span_find( xUrl, AZ_SPAN_FROM_STR( "/" ) );
    *pxHost = az_span_slice( xUrl, 0, lPathPosition );
    *pxPath = az_span_slice_to_end( xUrl, lPathPosition );
}

void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
}

void update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

static void downloadAndWriteToFlash(void)
{
  az_span urlHostSpan;
  az_span urlPathSpan;
  prvParseAduUrl(
      xBaseUpdateRequest.file_urls[ 0 ].url,
      &urlHostSpan, &urlPathSpan );

  WiFiClient client;

  httpUpdate.onStart(update_started);
  httpUpdate.onEnd(update_finished);
  httpUpdate.onProgress(update_progress);
  httpUpdate.onError(update_error);

  // TODO: Add SHA check of image (don't immediately reboot)

  /* TODO: remove this hack. */
  char pcNullTerminatedHost[ 128 ];
  ( void ) memcpy( pcNullTerminatedHost, az_span_ptr( urlHostSpan ), az_span_size( urlHostSpan ) );
  pcNullTerminatedHost[ az_span_size( urlHostSpan ) ] = '\0';

  /* TODO: remove this hack. */
  char nullTerminatedPath[ 128 ];
  ( void ) memcpy( nullTerminatedPath, az_span_ptr( urlPathSpan ), az_span_size( urlPathSpan ) );
  nullTerminatedPath[ az_span_size( urlPathSpan ) ] = '\0';

  t_httpUpdate_return ret = httpUpdate.update(client, pcNullTerminatedHost, 80, nullTerminatedPath);
}

static void verifyImageAndReboot(void)
{

}

// request_all_properties sends a request to Azure IoT Hub to request all properties for
// the device.  This call does not block.  Properties will be received on
// a topic previously subscribed to (AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC.)
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
  if(az_result_failed(rc))
  {
    Logger.Info("Failed to get the property document topic");
  }

  if (esp_mqtt_client_publish(
        mqtt_client,
        property_document_topic_buffer,
        NULL,
        0,
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

// send_adu_device_reported_property writes a property payload reporting device state and then sends it to
// Azure IoT Hub.
static void send_adu_device_information_property(void)
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
  if(az_result_failed(rc))
  {
    Logger.Error("Failed to get the property update topic");
  }

  // Write the updated reported property message.
  char reported_property_payload_buffer[SAMPLE_MQTT_PAYLOAD_LENGTH] = { 0 };
  az_span reported_property_payload = AZ_SPAN_FROM_BUFFER(reported_property_payload_buffer);
  az_json_writer adu_payload;

  rc = az_json_writer_init(&adu_payload, reported_property_payload, NULL);
  if(az_result_failed(rc))
  {
    Logger.Error("Failed to get the adu device information payload");
  }

  rc = az_iot_adu_client_get_agent_state_payload(
    &adu_client,
    &adu_device_information,
    AZ_IOT_ADU_CLIENT_AGENT_STATE_IDLE,
    NULL,
    NULL,
    &adu_payload);
  if(az_result_failed(rc))
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

static void send_adu_accept_manifest_property(int32_t version_number)
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
  if(az_result_failed(rc))
  {
    Logger.Error("Could not get properties topic");
    return;
  }

  char property_payload_buffer[SAMPLE_MQTT_PAYLOAD_LENGTH];
  az_span property_buffer = AZ_SPAN_FROM_BUFFER(property_payload_buffer);

  az_json_writer adu_payload;

  rc = az_json_writer_init(&adu_payload, property_buffer, NULL);
  if(az_result_failed(rc))
  {
    Logger.Error("Failed to get the adu device information payload");
  }

  rc = az_iot_adu_client_get_service_properties_response(
    &adu_client,
    version_number,
    200,
    &adu_payload );
  if(az_result_failed(rc))
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

// process_device_property_message handles incoming properties from Azure IoT Hub.
static void process_device_property_message(
    az_span message_span,
    az_iot_hub_client_properties_message_type message_type)
{
  az_json_reader jr;
  az_json_reader jr_adu_manifest;
  az_result rc = az_json_reader_init(&jr, message_span, NULL);
  if(az_result_failed(rc))
  {
    Logger.Error("Could not initialize json reader");
    return;
  }

  int32_t version_number;
  rc = az_iot_hub_client_properties_get_properties_version(
      &hub_client, &jr, message_type, &version_number);
  if(az_result_failed(rc))
  {
    Logger.Error("Could not get property version");
    return;
  }

  rc = az_json_reader_init(&jr, message_span, NULL);
  if(az_result_failed(rc))
  {
    Logger.Error("Could not initialize json reader");
    return;
  }

  az_span component_name;

  az_span xScratchBufferSpan = az_span_create( (uint8_t*)adu_scratch_buffer, ( int32_t ) sizeof(adu_scratch_buffer) );

  // Applications call az_iot_hub_client_properties_get_next_component_property to enumerate
  // properties received.
  while (az_result_succeeded(az_iot_hub_client_properties_get_next_component_property(
      &hub_client, &jr, message_type, AZ_IOT_HUB_CLIENT_PROPERTY_WRITABLE, &component_name)))
  {
    if( az_iot_adu_client_is_component_device_update(&adu_client, component_name))
    {
      // ADU Component
      rc = az_iot_adu_client_parse_service_properties(
        &adu_client,
        &jr,
        xScratchBufferSpan,
        &xBaseUpdateRequest,
        &xScratchBufferSpan );

      if( az_result_failed( rc ) )
      {
          Logger.Error( "az_iot_adu_client_parse_service_properties failed" + String(rc));
          return;
      }
      else
      {
          rc = az_json_reader_init(&jr_adu_manifest, xBaseUpdateRequest.update_manifest, NULL);

          if( az_result_failed( rc ) )
          {
              Logger.Error( "az_iot_adu_client_parse_update_manifest failed" + String(rc));
              return;
          }

          rc = az_iot_adu_client_parse_update_manifest(
              &adu_client,
              &jr_adu_manifest,
              &xBaseUpdateManifest );

          if( az_result_failed( rc ) )
          {
              Logger.Error( "az_iot_adu_client_parse_update_manifest failed" + String(rc));
              return;
          }

          Logger.Info("Parsed Azure device update manifest.");

          rc = SampleJWS::ManifestAuthenticate(xBaseUpdateRequest.update_manifest,
                                          xBaseUpdateRequest.update_manifest_signature,
                                          AZ_SPAN_FROM_BUFFER(adu_verification_buffer));
          if( az_result_failed( rc ) )
          {
              Logger.Error( "ManifestAuthenticate failed " + String(rc));
              return;
          }

          //TODO: Send a reject confirmation if auth doesn't work out.
          //TODO: Add a step for accept/deny request like in middleware.

          Logger.Info("Manifest authenticated successfully");

          Logger.Info("Sending manifest property accept");
          send_adu_accept_manifest_property(version_number);

          did_parse_update = true;
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

// handle_device_property_message handles incoming properties from Azure IoT Hub.
static void handle_device_property_message(
    byte* payload,
    unsigned int length,
    az_iot_hub_client_properties_message const* property_message)
{
  az_span const message_span = az_span_create((uint8_t*)payload, length);

  // Invoke appropriate action per message type (3 types only).
  switch (property_message->message_type)
  {
    // A message from a property GET publish message with the property document as a payload.
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
      Logger.Info("Message Type: IoT Hub has acknowledged properties that the device sent");
      break;

    // An error has occurred
    case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_ERROR:
      Logger.Error("Message Type: Request Error");
      break;
  }
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
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
    Logger.Error("Error: " + String(rc) + " Received a message from an unknown topic: " + String(topic));
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
      Logger.Info("ESP Error " + String(esp_err_to_name(event->error_handle->esp_tls_last_esp_err)));
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

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe to properties messages.");
      }
      else
      {
        Logger.Info("Subscribed to properties messages; message id:"  + String(r));
      }

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_PROPERTIES_WRITABLE_UPDATES_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe to writable property updates.");
      }
      else
      {
        Logger.Info("Subscribed to writable property updates; message id:"  + String(r));
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

      // Chunked data will not have a topic. Only copy when topic length is great than 0.
      if(event->topic_len > 0)
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
      // Logger.Info("Data: " + String(incoming_data));

      if(event->total_data_len > event->data_len)
      {
        Logger.Info("Received Chunked Payload Data");
        chunked_data_index = event->current_data_offset != 0 ? chunked_data_index : 0;
        // This data is going to be incoming in chunks. Piece it together.
        memcpy((void*)&adu_scratch_buffer[chunked_data_index], event->data, event->data_len);
        chunked_data_index += event->data_len;

        if(chunked_data_index == event->total_data_len)
        {
          Logger.Info("Received all of the chunked data. Moving to process.");
          adu_scratch_buffer[chunked_data_index] = '\0';
          Logger.Info("Data: " + String(adu_scratch_buffer));
          receivedCallback(incoming_topic, (byte*)adu_scratch_buffer, event->total_data_len);
        }
      }
      else
      {
          receivedCallback(incoming_topic, (byte*)incoming_data, event->data_len);
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

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);
  options.model_id = AZ_SPAN_FROM_STR(AZ_IOT_ADU_DTMI);
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

  if(az_result_failed(az_iot_adu_client_init(&adu_client, NULL)))
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

static int initializeMqttClient()
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
static uint32_t getEpochTimeInSecs() 
{ 
  return (uint32_t)time(NULL);
}

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}


static void getTelemetryPayload(az_span payload, az_span* out_payload)
{
  az_result rc;
  az_span original_payload = payload;

  payload = az_span_copy(
      payload, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
  rc = az_span_u32toa(payload, telemetry_send_count++, &payload);
  (void)rc;
  payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" }"));
  payload = az_span_copy_u8(payload, '\0');

  *out_payload = az_span_slice(original_payload, 0, az_span_size(original_payload) - az_span_size(payload) - 1);
}

static void sendTelemetry()
{
  az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

  Logger.Info("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &hub_client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  getTelemetryPayload(telemetry, &telemetry);

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
  establishConnection();
}

static int waitCount = 1;

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Logger.Info("Connecting to WiFI");
    connectToWiFi();
  }
  #ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
  #endif
  else if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;

    if (waitCount % 5 == 0)
    {
      Logger.Info("Requesting all device properties");
      request_all_properties();
      Logger.Info("Sending ADU device information");
      send_adu_device_information_property();
    }

    if(did_parse_update)
    {
      downloadAndWriteToFlash();
      verifyImageAndReboot();
    }

    waitCount++;
  }
}
