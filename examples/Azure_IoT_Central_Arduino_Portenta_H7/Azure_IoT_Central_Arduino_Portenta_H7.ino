// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Central sample specific for Arduino Portenta H7.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c and https://azureiotcentral.com/.
 * 
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 * 
 * The additional layers in this sketch provide a structured use of azure-sdk-for-c and
 * the MQTT client of your choice to perform all the steps needed to connect and interact with 
 * Azure IoT Central.
 * 
 * AzureIoT.cpp contains a state machine that implements those steps, plus abstractions to simplify
 * its overall use. Besides the basic configuration needed to access the Azure IoT services,
 * all that is needed is to provide the functions required by that layer to:
 * - Interact with your MQTT client,
 * - Perform data manipulations (HMAC SHA256 encryption, Base64 decoding and encoding),
 * - Receive the callbacks for Plug and Play properties and commands.
 * 
 * Azure_IoT_PnP_Template.cpp contains the actual implementation of the IoT Plug and Play template
 * specific for the Arduino Portenta H7 board.
 * 
 * To properly connect to your Azure IoT services, please fill the information in the `iot_configs.h` file. 
 */

/* --- Dependencies --- */
// C99 libraries
#include <cstdlib>
#include <cstdarg>
#include <string.h>
#include <time.h>

// For hmac SHA256 encryption
#include <ECCX08.h>

// Libraries for SSL client, MQTT client, NTP, and WiFi connection
#include <ArduinoBearSSL.h>
#include <ArduinoMqttClient.h>
#include <NTPClient_Generic.h>
#include <TimeLib.h>
#include <WiFi.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>

// Additional sample headers 
#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"
#include "iot_configs.h"

/* --- Sample-specific Settings --- */
#define MQTT_RETAIN_MSG true
#define MQTT_DO_NOT_RETAIN_MSG !MQTT_RETAIN_MSG
#define SERIAL_LOGGER_BAUD_RATE 115200

/* --- Time and NTP Settings --- */
#define GMT_OFFSET_SECS (IOT_CONFIG_DAYLIGHT_SAVINGS ? \
                        ((IOT_CONFIG_TIME_ZONE + IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR) : \
                        (IOT_CONFIG_TIME_ZONE * SECS_PER_HOUR))

/* --- Function Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

/* --- Function Declarations --- */
static void sync_device_clock_with_ntp_server();
static void connect_to_wifi();
static void on_message_received(int message_size);
static String get_formatted_date_time(uint32_t epoch_time_in_seconds); 

// This is a logging function used by Azure IoT client.
static void logging_function(log_level_t log_level, char const* const format, ...);

/* --- Sample variables --- */
static azure_iot_config_t azure_iot_config;
static azure_iot_t azure_iot;
static WiFiUDP wifi_udp_client;
static NTPClient ntp_client(wifi_udp_client);
static WiFiClient wifi_client;
static BearSSLClient bear_ssl_client(wifi_client);
static MqttClient arduino_mqtt_client(bear_ssl_client);

#define AZ_IOT_DATA_BUFFER_SIZE 1500
static uint8_t az_iot_data_buffer[AZ_IOT_DATA_BUFFER_SIZE];

static uint8_t message_buffer[AZ_IOT_DATA_BUFFER_SIZE];

static uint32_t properties_request_id = 0;
static bool send_device_info = true;


/* --- MQTT Interface Functions --- */
/*
 * These functions are used by Azure IoT to interact with whatever MQTT client used by the sample
 * (in this case, ArduinoMqttClient). Please see the documentation in AzureIoT.h for more details.
 */

/*
 * See the documentation of `mqtt_client_init_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_init_function(mqtt_client_config_t* mqtt_client_config, mqtt_client_handle_t *mqtt_client_handle)
{
  int result;

  const char* client_id = (const char*)az_span_ptr(mqtt_client_config->client_id);
  const char* username = (const char*)az_span_ptr(mqtt_client_config->username);
  const char* password = (const char*)az_span_ptr(mqtt_client_config->password);
  int port = mqtt_client_config->port;
 
  // Address for DPS is az_span from string literal (#define DPS_GLOBAL_ENDPOINT_FQDN). Null terminated.
  // Address for Hub is az_span, retrieved from message from DPS. Not null terminated.
  // mqtt_client_init_function() is called in both scenarios.
  char address[128] = {0}; // Default to null-termination.
  memcpy(address, az_span_ptr(mqtt_client_config->address), az_span_size(mqtt_client_config->address));

  arduino_mqtt_client.setId(client_id);
  arduino_mqtt_client.setUsernamePassword(username, password);
  arduino_mqtt_client.setCleanSession(true);
  arduino_mqtt_client.onMessage(on_message_received);

  LogInfo("MQTT Client ID: %s", client_id);
  LogInfo("MQTT Username: %s", username);
  LogInfo("MQTT Password: ***");
  LogInfo("MQTT client address: %s", address);
  LogInfo("MQTT client port: %d", port);

  while (!arduino_mqtt_client.connect(address, port)) 
  {
    int code = arduino_mqtt_client.connectError();
    LogError("Cannot connect. Error Code: %d", code);
    delay(5000);
  }

  LogInfo("MQTT client connected.");

  result = azure_iot_mqtt_client_connected(&azure_iot);
  if (result != RESULT_OK)
  {
    LogError("Failed updating azure iot client of MQTT connection.");
  }
  else
  {
    *mqtt_client_handle = &arduino_mqtt_client;
  }

  return result;
}

/*
 * See the documentation of `mqtt_client_deinit_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_deinit_function(mqtt_client_handle_t mqtt_client_handle)
{
  int result;
  MqttClient* arduino_mqtt_client_handle = (MqttClient*)mqtt_client_handle;

  LogInfo("MQTT client being disconnected.");

  arduino_mqtt_client_handle->stop();

  result = azure_iot_mqtt_client_disconnected(&azure_iot);
  if (result != RESULT_OK)
  {
    LogError("Failed updating azure iot client of MQTT disconnection.");      
  }

  return result;
}

/*
 * See the documentation of `mqtt_client_subscribe_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_subscribe_function(mqtt_client_handle_t mqtt_client_handle, az_span topic, mqtt_qos_t qos)
{
  LogInfo("MQTT client subscribing to '%.*s'", az_span_size(topic), az_span_ptr(topic));
   
  int result;
  MqttClient* arduino_mqtt_client_handle = (MqttClient*)mqtt_client_handle;

  int mqtt_result = arduino_mqtt_client_handle->subscribe((const char*)az_span_ptr(topic), (uint8_t)qos);
  
  if (mqtt_result == 1) // ArduinoMqttClient: 1 on success, 0 on failure
  {
      LogInfo("MQTT topic subscribed");

      int packet_id = 0; // packet id is private in ArduinoMqttClient library.
      result = azure_iot_mqtt_client_subscribe_completed(&azure_iot, packet_id);
      if (result != RESULT_OK)
      {
        LogError("Failed updating azure iot client of MQTT subscribe.");
      }
  }
  else
  {
    LogError("ArduinoMqttClient subscribe failed.");
    result = RESULT_ERROR;
  }
  
  return result;
}

/*
 * See the documentation of `mqtt_client_publish_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_publish_function(mqtt_client_handle_t mqtt_client_handle, mqtt_message_t* mqtt_message)
{
  LogInfo("MQTT client publishing to '%s'", az_span_ptr(mqtt_message->topic));

  int result;
  MqttClient* arduino_mqtt_client_handle = (MqttClient*)mqtt_client_handle;

  int mqtt_result = arduino_mqtt_client_handle->beginMessage(
                        (const char*)az_span_ptr(mqtt_message->topic), 
                        MQTT_DO_NOT_RETAIN_MSG, 
                        (uint8_t)mqtt_message->qos);

  if (mqtt_result == 1) // ArduinoMqttClient: 1 on success, 0 on failure 
  {
    arduino_mqtt_client_handle->print((const char*)az_span_ptr(mqtt_message->payload));
    mqtt_result = arduino_mqtt_client_handle->endMessage();
    if (mqtt_result == 1)
    {
      int packet_id = 0; // packet id is private in ArduinoMqttClient library.
      result = azure_iot_mqtt_client_publish_completed(&azure_iot, packet_id);
      if (result != RESULT_OK)
      {
        LogError("Failed updating azure iot client of MQTT publish.");
      }
    }
    else
    {
      LogError("ArduinoMqttClient endMessage failed.");
      result = RESULT_ERROR;
    }
  }
  else
  {
    LogError("ArduinoMqttClient beginMessage failed.");
    result = RESULT_ERROR;
  }
 
  return result;
}

/* --- Other Interface functions required by Azure IoT --- */

/*
 * See the documentation of `hmac_sha256_encryption_function_t` in AzureIoT.h for details.
 */
static int eccx08_hmac_sha256(const uint8_t* key, size_t key_length, const uint8_t* payload, size_t payload_length, uint8_t* signed_payload, size_t signed_payload_size)
{
  (void)signed_payload_size;

  // HMAC-SHA256 sign the signature with the decoded device key.
  ECCX08.begin();
  ECCX08.nonce(key);
  ECCX08.beginHMAC(0xFFFF);
  ECCX08.updateHMAC(payload, payload_length);
  ECCX08.endHMAC(signed_payload);
  
  return 0;
}

/*
 * See the documentation of `base64_decode_function_t` in AzureIoT.h for details.
 */
static int base64_decode(uint8_t* data, size_t data_length, uint8_t* decoded, size_t decoded_size, size_t* decoded_length)
{
  az_span dataSpan = az_span_create(data, data_length);
  az_span decodedSpan = az_span_create(decoded, decoded_size);
  
  if (az_base64_decode(decodedSpan, dataSpan, (int32_t*)decoded_length) == AZ_OK) {
    return 0;
  }
  return 1;
}

/*
 * See the documentation of `base64_encode_function_t` in AzureIoT.h for details.
 */
static int base64_encode(uint8_t* data, size_t data_length, uint8_t* encoded, size_t encoded_size, size_t* encoded_length)
{
  az_span dataSpan =az_span_create(data, data_length);
  az_span encodedSpan = az_span_create(encoded, encoded_size);
  
  if (az_base64_encode(encodedSpan, dataSpan, (int32_t*)encoded_length) == AZ_OK) {
    return 0;
  }
  return 1;
}

/*
 * See the documentation of `properties_update_completed_t` in AzureIoT.h for details.
 */
static void on_properties_update_completed(uint32_t request_id, az_iot_status status_code)
{
  LogInfo("Properties update request completed (id=%d, status=%d)", request_id, status_code);
}

/*
 * See the documentation of `properties_received_t` in AzureIoT.h for details.
 */
void on_properties_received(az_span properties)
{
  LogInfo("Properties update received: %.*s", az_span_size(properties), az_span_ptr(properties));

  // It is recommended not to perform work within callbacks.
  // The properties are being handled here to simplify the sample.
  if (azure_pnp_handle_properties_update(&azure_iot, properties, properties_request_id++) != 0)
  {
    LogError("Failed handling properties update.");
  }
}

/*
 * See the documentation of `command_request_received_t` in AzureIoT.h for details.
 */
static void on_command_request_received(command_request_t command)
{  
  az_span component_name = az_span_size(command.component_name) == 0 ? AZ_SPAN_FROM_STR("") : command.component_name;
  
  LogInfo("Command request received (id=%.*s, component=%.*s, name=%.*s)", 
    az_span_size(command.request_id), az_span_ptr(command.request_id),
    az_span_size(component_name), az_span_ptr(component_name),
    az_span_size(command.command_name), az_span_ptr(command.command_name));

  // Here the request is being processed within the callback that delivers the command request.
  // However, for production application the recommendation is to save `command` and process it outside
  // this callback, usually inside the main thread/task/loop.
  (void)azure_pnp_handle_command_request(&azure_iot, command);
}

/*
 * See the documentation of `get_time_t` in AzureIoT.h for details.
 */
static uint32_t get_time()
{
    return (uint32_t)ntp_client.getUTCEpochTime();
}

/* --- Arduino setup and loop Functions --- */
void setup()
{
  while (!Serial);
  Serial.begin(SERIAL_LOGGER_BAUD_RATE);
  set_logging_function(logging_function);

  connect_to_wifi();
  sync_device_clock_with_ntp_server();
  ArduinoBearSSL.onGetTime(get_time); // Required for server trusted root validation.

  azure_pnp_init();
  /* 
   * The configuration structure used by Azure IoT must remain unchanged (including data buffer) 
   * throughout the lifetime of the sample. This variable must also not lose context so other
   * components do not overwrite any information within this structure.
   */
  azure_iot_config.user_agent = AZ_SPAN_FROM_STR(IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT);
  azure_iot_config.model_id = azure_pnp_get_model_id();
  azure_iot_config.use_device_provisioning = true; // Required for Azure IoT Central.
  azure_iot_config.iot_hub_fqdn = AZ_SPAN_EMPTY;
  azure_iot_config.device_id = AZ_SPAN_EMPTY;
  azure_iot_config.device_certificate = AZ_SPAN_EMPTY;
  azure_iot_config.device_certificate_private_key = AZ_SPAN_EMPTY;
  azure_iot_config.device_key = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY);
  azure_iot_config.dps_id_scope = AZ_SPAN_FROM_STR(IOT_CONFIG_DPS_ID_SCOPE);
  azure_iot_config.dps_registration_id = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID); // Use Device ID for Azure IoT Central.
  azure_iot_config.data_buffer = AZ_SPAN_FROM_BUFFER(az_iot_data_buffer);
  azure_iot_config.sas_token_lifetime_in_minutes = IOT_CONFIG_MQTT_PASSWORD_LIFETIME_IN_MINUTES;
  azure_iot_config.mqtt_client_interface.mqtt_client_init = mqtt_client_init_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_deinit = mqtt_client_deinit_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_subscribe = mqtt_client_subscribe_function;
  azure_iot_config.mqtt_client_interface.mqtt_client_publish = mqtt_client_publish_function;
  azure_iot_config.data_manipulation_functions.hmac_sha256_encrypt = eccx08_hmac_sha256;
  azure_iot_config.data_manipulation_functions.base64_decode = base64_decode;
  azure_iot_config.data_manipulation_functions.base64_encode = base64_encode;
  azure_iot_config.on_properties_update_completed = on_properties_update_completed;
  azure_iot_config.on_properties_received = on_properties_received;
  azure_iot_config.on_command_request_received = on_command_request_received;
  azure_iot_config.get_time = get_time;

  azure_iot_init(&azure_iot, &azure_iot_config);
  azure_iot_start(&azure_iot);

  LogInfo("Azure IoT client initialized (state=%d)", azure_iot.state);
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
        if (send_device_info)
        {
          (void)azure_pnp_send_device_info(&azure_iot, properties_request_id++);
           send_device_info = false; // Only need to send once.
        }
        else if (azure_pnp_send_telemetry(&azure_iot) != 0)
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

    // MQTT loop must be called to process Telemetry and Cloud-to-Device (C2D) messages.
    arduino_mqtt_client.poll();
    ntp_client.update();
    delay(50);

    azure_iot_do_work(&azure_iot);
  }
}


/* === Function Implementations === */

/*
 * These are support functions used by the sample itself to perform its basic tasks
 * of connecting to the internet, syncing the board clock, MQTT client callback 
 * and logging.
 */

/* --- System and Platform Functions --- */
static void sync_device_clock_with_ntp_server()
{
  LogInfo("Setting time using SNTP");

  ntp_client.begin();
  while (!ntp_client.forceUpdate()) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  LogInfo("Time initialized!");
}

static void connect_to_wifi()
{
  LogInfo("Connecting to WIFI wifi_ssid %s", IOT_CONFIG_WIFI_SSID);

  WiFi.begin(IOT_CONFIG_WIFI_SSID, IOT_CONFIG_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  LogInfo("WiFi connected, IP address: %u", WiFi.localIP());
}

void on_message_received(int message_size) 
{
  LogInfo("MQTT message received.");

  mqtt_message_t mqtt_message;

  // Copy message topic. Avoids any inadvertant ArduinoMqttClient _rxState or _rxMessageTopic changes.
  // messageTopic() must be called before read();
  String message_topic = arduino_mqtt_client.messageTopic();

  arduino_mqtt_client.read(message_buffer, (size_t)message_size);

  mqtt_message.topic = az_span_create((uint8_t*)message_topic.c_str(), message_topic.length());
  mqtt_message.payload = az_span_create(message_buffer, message_size);
  mqtt_message.qos = mqtt_qos_at_most_once; // QoS is unused by azure_iot_mqtt_client_message_received. 

  if (azure_iot_mqtt_client_message_received(&azure_iot, &mqtt_message) != 0)
  {
    LogError("azure_iot_mqtt_client_message_received failed (topic=%s).", arduino_mqtt_client.messageTopic().c_str());
  }
}

static String get_formatted_date_time(uint32_t epoch_time_in_seconds) 
{
  char buffer[256];

  time_t time = (time_t)epoch_time_in_seconds;
  struct tm* timeInfo = localtime(&time);

  strftime(buffer, 20, "%F %T", timeInfo);

  return String(buffer);
}

static void logging_function(log_level_t log_level, char const* const format, ...)
{
  Serial.print(get_formatted_date_time(get_time() + GMT_OFFSET_SECS));

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
