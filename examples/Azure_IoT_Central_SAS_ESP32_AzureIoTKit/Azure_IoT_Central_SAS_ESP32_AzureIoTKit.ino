// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 * 
 * To connect and work with Azure IoT Hub you need a MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is a MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 * 
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h` file. 
 */

/* --- Dependencies --- */

// C99 libraries
#include <cstdlib>
#include <cstdarg>
#include <string.h>
#include <time.h>

// For hmac SHA256 encryption
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers 
// #define DISABLE_LOGGING 1
#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"
#include "iot_configs.h"

/* --- Sample-specific Settings --- */
#define SERIAL_LOGGER_BAUD_RATE 115200

/* --- Time and NTP Settings --- */
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF   1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

#define UNIX_TIME_NOV_13_2017 1510592825
#define UNIX_EPOCH_START_YEAR 1900

/* --- Handling iot_config.h Settings --- */
static const char* wifi_ssid = IOT_CONFIG_WIFI_SSID;
static const char* wifi_password = IOT_CONFIG_WIFI_PASSWORD;

// TODO: remove this after updating library with Azure SDK for C version 1.3.0-beta.2 or later.
#define AZURE_SDK_CLIENT_USER_AGENT_WORKAROUND "DeviceClientType=" AZURE_SDK_CLIENT_USER_AGENT

/* --- Function Declarations --- */
static void sync_device_clock_with_ntp_server();
static void connect_to_wifi();
static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event);

// This is a logging function used by Azure IoT client.
#ifndef DISABLE_LOGGING
static void logging_function(log_level_t log_level, char const* const format, ...);
#endif

/* --- Sample variables --- */
static azure_iot_config_t azure_iot_config;
static azure_iot_t azure_iot;
static esp_mqtt_client_handle_t mqtt_client;

static char mqtt_broker_uri[128];

#define AZ_IOT_DATA_BUFFER_SIZE 1000 
static uint8_t az_iot_data_buffer[AZ_IOT_DATA_BUFFER_SIZE];

#define MQTT_PROTOCOL_PREFIX "mqtts://"


/* --- MQTT Interface Functions --- */
/*
 * These functions are used by Azure IoT to interact with whatever MQTT client used by the sample (in this case, Espressif's ESP MQTT).
 * Please see the documentation in Azure_IoT.h for more details.
 */

static int mqtt_client_init_function(mqtt_client_config_t* mqtt_client_config, mqtt_client_handle_t *mqtt_client_handle)
{
  int result;
  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));  

  az_span mqtt_broker_uri_span = AZ_SPAN_FROM_BUFFER(mqtt_broker_uri);
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, AZ_SPAN_FROM_STR(MQTT_PROTOCOL_PREFIX));
  mqtt_broker_uri_span = az_span_copy(mqtt_broker_uri_span, mqtt_client_config->address);
  az_span_copy_u8(mqtt_broker_uri_span, null_terminator);

  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_client_config->port;
  mqtt_config.client_id = (const char*)az_span_ptr(mqtt_client_config->client_id);
  mqtt_config.username = (const char*)az_span_ptr(mqtt_client_config->username);
  mqtt_config.password = (const char*)az_span_ptr(mqtt_client_config->password);
  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = esp_mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.buffer_size = 2048;
  mqtt_config.cert_pem = (const char*)ca_pem;

  LogInfo("MQTT client target uri set to '%s'", mqtt_broker_uri);

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    LogError("esp_mqtt_client_init failed.");
    result = 1;
  }
  else
  {
    esp_err_t start_result = esp_mqtt_client_start(mqtt_client);
  
    if (start_result != ESP_OK)
    {
      LogError("esp_mqtt_client_start failed (error code: 0x%08x).", start_result);
      result = 1;
    }
    else
    {
      *mqtt_client_handle = mqtt_client;
      result = 0;
    }
  }

  return result;
}

static int mqtt_client_deinit_function(mqtt_client_handle_t mqtt_client_handle)
{
  int result = 0;
  esp_mqtt_client_handle_t esp_mqtt_client_handle = (esp_mqtt_client_handle_t)mqtt_client_handle;

  LogInfo("MQTT client being disconnected.");
  
  if (esp_mqtt_client_stop(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed stopping MQTT client.");
  }

  if (esp_mqtt_client_destroy(esp_mqtt_client_handle) != ESP_OK)
  {
    LogError("Failed destroying MQTT client.");
  }

  if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
  {
    LogError("Failed updating azure iot client of MQTT disconnection.");      
  }

  return 0;
}

static int mqtt_client_subscribe_function(mqtt_client_handle_t mqtt_client_handle, const uint8_t* topic, size_t topic_lenght, mqtt_qos_t qos)
{
  LogInfo("MQTT client subscribing to '%s'", topic);
       
  // As per documentation, `topic` always ends with a null-terminator.
  // esp_mqtt_client_subscribe returns the packet id or negative on error already, so no conversion is needed.
  int packet_id = esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)mqtt_client_handle, (const char*)topic, (int)qos);

  return packet_id;
}

static int mqtt_client_publish_function(mqtt_client_handle_t mqtt_client_handle, mqtt_message_t* mqtt_message)
{
  LogInfo("MQTT client publishing to '%s'", az_span_ptr(mqtt_message->topic));

  int mqtt_result = esp_mqtt_client_publish(
    (esp_mqtt_client_handle_t)mqtt_client_handle, 
    (const char*)az_span_ptr(mqtt_message->topic), // topic is always null-terminated.
    (const char*)az_span_ptr(mqtt_message->payload), 
    az_span_size(mqtt_message->payload),
    (int)mqtt_message->qos, 
    MQTT_DO_NOT_RETAIN_MSG);
  
  if (mqtt_result == -1)
  {
    return RESULT_ERROR;
  }
  else
  {
    return RESULT_OK;
  }
}

/* --- Other Interface functions required by Azure IoT --- */
static int mbedtls_hmac_sha256(const uint8_t* key, size_t key_length, const uint8_t* payload, size_t payload_length, uint8_t* signed_payload, size_t signed_payload_size)
{
  (void)signed_payload_size;
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key, key_length);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)payload, payload_length);
  mbedtls_md_hmac_finish(&ctx, (byte*)signed_payload);
  mbedtls_md_free(&ctx);

  return 0;
}

static int base64_decode(uint8_t* data, size_t data_length, uint8_t* decoded, size_t decoded_size, size_t* decoded_length)
{
  return mbedtls_base64_decode(decoded, decoded_size, decoded_length, data, data_length);
}

static int base64_encode(uint8_t* data, size_t data_length, uint8_t* encoded, size_t encoded_size, size_t* encoded_length)
{
  return mbedtls_base64_encode(encoded, encoded_size, encoded_length, data, data_length);
}


/* --- Arduino setup and loop Functions --- */
void setup()
{
  Serial.begin(SERIAL_LOGGER_BAUD_RATE);
  set_logging_function(logging_function);

  connect_to_wifi();
  sync_device_clock_with_ntp_server();

  azure_pnp_init();

  /* 
   * The configuration structure used by Azure IoT must remain unchanged (including data buffer) 
   * throughout the lifetime of the sample. This variable must also not loose context so other
   * components do not overwrite any information within this structure.
   */
  azure_iot_config.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT_WORKAROUND);
  azure_iot_config.model_id = azure_pnp_get_model_id();
  azure_iot_config.use_device_provisioning = true;
  azure_iot_config.iot_hub_fqdn = AZ_SPAN_EMPTY;
  azure_iot_config.device_id = AZ_SPAN_EMPTY;
  azure_iot_config.device_key = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY);
  azure_iot_config.dps_id_scope = AZ_SPAN_FROM_STR(DPS_ID_SCOPE);
  azure_iot_config.dps_registration_id = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID); // Use Device ID for Azure IoT Central.
  azure_iot_config.data_buffer = AZ_SPAN_FROM_BUFFER(az_iot_data_buffer);
  azure_iot_config.mqtt_client_interface.mqtt_client_init = mqtt_client_init_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_deinit = mqtt_client_deinit_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_subscribe = mqtt_client_subscribe_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_publish = mqtt_client_publish_function;
  azure_iot_config.data_manipulation_functions.hmac_sha512_encrypt = mbedtls_hmac_sha256;
  azure_iot_config.data_manipulation_functions.base64_decode = base64_decode;
  azure_iot_config.data_manipulation_functions.base64_encode = base64_encode;

  if (azure_iot_init(&azure_iot, &azure_iot_config) != 0)
  {
    LogError("Failed initializing the Azure IoT client.");
  }
  else
  {
    LogInfo("Azure IoT client initialized (state=%d)", azure_iot.state);
    azure_iot_start(&azure_iot);
  }
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connect_to_wifi();
    azure_iot_start(&azure_iot);
  }
  else
  {
    switch(azure_iot_get_status(&azure_iot))
    {
      case azure_iot_connected:
        if (azure_pnp_send_telemetry(&azure_iot) != 0)
        {
          LogError("Failed sending telemetry.");          
        }

        break;
      case azure_iot_error:
        LogError("Azure IoT client is in error state." );
        azure_iot_stop(&azure_iot);
        WiFi.disconnect();
        break;
      default:
        break;
    }

    azure_iot_do_work(&azure_iot);
  }
}


/* === Function Implementations === */

/* --- System and Platform Functions --- */
static void sync_device_clock_with_ntp_server()
{
  LogInfo("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Serial.println("");
  LogInfo("Time initialized!");
}

static void connect_to_wifi()
{
  LogInfo("Connecting to WIFI wifi_ssid %s", wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  LogInfo("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
}

static esp_err_t esp_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
     LogError("MQTT client in ERROR state.");
      LogError( 
        "esp_tls_stack_err=%d; esp_tls_cert_verify_flags=%d;esp_transport_sock_errno=%d;error_type=%d;connect_return_code=%d",  
        event->error_handle->esp_tls_stack_err,
        event->error_handle->esp_tls_cert_verify_flags,
        event->error_handle->esp_transport_sock_errno,
        event->error_handle->error_type,
        event->error_handle->connect_return_code);

      switch (event->error_handle->connect_return_code) 
      {
        case MQTT_CONNECTION_ACCEPTED: 
          LogError("connect_return_code=MQTT_CONNECTION_ACCEPTED"); 
          break; 
        case MQTT_CONNECTION_REFUSE_PROTOCOL: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_PROTOCOL"); 
          break; 
        case MQTT_CONNECTION_REFUSE_ID_REJECTED: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_ID_REJECTED"); 
          break; 
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE"); 
          break; 
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_BAD_USERNAME"); 
          break; 
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED: 
          LogError("connect_return_code=MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED"); 
          break; 
        default: 
          LogError("connect_return_code=unknown"); 
          break; 
      };

      break;
    case MQTT_EVENT_CONNECTED:
      LogInfo("MQTT client connected (session_present=%d).", event->session_present);

      if (azure_iot_mqtt_client_connected(&azure_iot) != 0)
      {
        LogError("azure_iot_mqtt_client_connected failed.");
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      LogInfo("MQTT client disconnected.");

      if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
      {
        LogError("azure_iot_mqtt_client_disconnected failed.");
      }

      break;
    case MQTT_EVENT_SUBSCRIBED:
      LogInfo("MQTT topic subscribed (message id=%d).", event->msg_id);

      if (azure_iot_mqtt_client_subscribe_completed(&azure_iot, event->msg_id) != 0)
      {
        LogError("azure_iot_mqtt_client_subscribe_completed failed.");
      }

      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      LogInfo("MQTT topic unsubscribed.");
      break;
    case MQTT_EVENT_PUBLISHED:
      LogInfo("MQTT event MQTT_EVENT_PUBLISHED");

      if (azure_iot_mqtt_client_publish_completed(&azure_iot, event->msg_id) != 0)
      {
        LogError("azure_iot_mqtt_client_publish_completed failed.");
      }

      break;
    case MQTT_EVENT_DATA:
      LogInfo("MQTT message received.");

      mqtt_message_t mqtt_message;
      mqtt_message.topic = az_span_create((uint8_t*)event->topic, event->topic_len);
      mqtt_message.payload = az_span_create((uint8_t*)event->data, event->data_len);
      mqtt_message.qos = mqtt_qos_at_most_once; // QoS is unused by azure_iot_mqtt_client_message_received. 

      if (azure_iot_mqtt_client_message_received(&azure_iot, &mqtt_message) != 0)
      {
        LogError("azure_iot_mqtt_client_subscribe_completed failed.");
      }

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      LogInfo("MQTT client connecting.");
      break;
    default:
      LogError("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

#ifndef DISABLE_LOGGING
static void logging_function(log_level_t log_level, char const* const format, ...)
{
  struct tm* ptm;
  time_t now = time(NULL);

  ptm = gmtime(&now);

  Serial.print(ptm->tm_year + UNIX_EPOCH_START_YEAR);
  Serial.print("/");
  Serial.print(ptm->tm_mon + 1);
  Serial.print("/");
  Serial.print(ptm->tm_mday);
  Serial.print(" ");

  if (ptm->tm_hour < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_hour);
  Serial.print(":");

  if (ptm->tm_min < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_min);
  Serial.print(":");

  if (ptm->tm_sec < 10)
  {
    Serial.print(0);
  }

  Serial.print(ptm->tm_sec);

  Serial.print(log_level == log_level_info ? " [INFO] " : " [ERROR] ");

  char message[256];
  va_list ap;
  va_start(ap, format);
  int message_length = vsnprintf(message, 256, format, ap);
  va_end(ap);

  if (message_length < 0)
  {
    Serial.println("Failed encoding log message (!)");
  }
  else
  {
    Serial.println(message);
  }
}
#endif // DISABLE_LOGGING