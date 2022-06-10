// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Central sample specific for Arduino Nano RP2040 Connect.
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
 * specific for the Arduino Nano RP2040 Connect board.
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
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

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
#define MQTT_DO_NOT_RETAIN_MSG  0

/* --- AzureIoT.cpp Function Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

// Time and Time Zone.
#define GMT_OFFSET_SECS (IOT_CONFIG_DAYLIGHT_SAVINGS ? \
                        ((IOT_CONFIG_TIME_ZONE + IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR) : \
                        (IOT_CONFIG_TIME_ZONE * SECS_PER_HOUR))

                        
/* --- Function Declarations --- */
static void connect_to_wifi();
static String mqttErrorCodeName(int errorCode); 
void onMessageReceived(int messageSize);

// This is a logging function used by Azure IoT client.
static void logging_function(log_level_t log_level, char const* const format, ...);

/* --- Sample variables --- */
static azure_iot_config_t azure_iot_config;
static azure_iot_t azure_iot;
static WiFiUDP wiFiUDPClient;
static NTPClient ntpClient(wiFiUDPClient);
static WiFiClient wiFiClient;
static BearSSLClient bearSSLClient(wiFiClient);
static MqttClient mqttClient(bearSSLClient);

static char mqtt_broker_uri[128];

#define AZ_IOT_DATA_BUFFER_SIZE 1500
static uint8_t az_iot_data_buffer[AZ_IOT_DATA_BUFFER_SIZE];

static uint8_t message_buffer[AZ_IOT_DATA_BUFFER_SIZE];

#define MQTT_PROTOCOL_PREFIX "ssl://"

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
  int result = 0; //success

  const char* clientId = (const char*)az_span_ptr(mqtt_client_config->client_id);
  const char* username = (const char*)az_span_ptr(mqtt_client_config->username);
  const char* password = (const char*)az_span_ptr(mqtt_client_config->password);

  mqttClient.setId(clientId);
  mqttClient.setUsernamePassword(username, password);
  mqttClient.setCleanSession(true);
  mqttClient.onMessage(onMessageReceived);

  LogInfo("MQTT Client ID: %s", clientId);
  LogInfo("MQTT Username: %s", username);
  LogInfo("MQTT Password: %s", password);
  LogInfo("MQTT client address: %s", mqtt_client_config->address);
  LogInfo("MQTT client port: %d", mqtt_client_config->port);

  char address[128] = {0};
  memcpy(address, az_span_ptr(mqtt_client_config->address), az_span_size(mqtt_client_config->address));  // az_span does not include null termination. Need that!

  while (!mqttClient.connect(address, mqtt_client_config->port)) 
  {
    int code = mqttClient.connectError();
    LogError("Cannot connect. Reason: %s, Code: %d", mqttErrorCodeName(code), code);
    delay(5000);
  }

  LogInfo("is mqtt client connected? %u", mqttClient.connected());

  //const char* topic2 = "dps/registrations/res/#";
  //int rc = mqttClient.subscribe(AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC);
  //LogInfo("rc of mqttClient.subscribe: %d", rc);
  
  LogInfo("MQTT client connected.");

  if (azure_iot_mqtt_client_connected(&azure_iot) != 0)
  {
    LogError("azure_iot_mqtt_client_connected failed.");
  }

  *mqtt_client_handle = &mqttClient;

  return result;
}

/*
 * See the documentation of `mqtt_client_deinit_function_t` in AzureIoT.h for details.
 */
static int mqtt_client_deinit_function(mqtt_client_handle_t mqtt_client_handle)
{
  int result = 0; // success

  LogInfo("MQTT client being disconnected.");

  ((MqttClient*)mqtt_client_handle)->stop();

  if (azure_iot_mqtt_client_disconnected(&azure_iot) != 0)
  {
    LogError("Failed updating azure iot client of MQTT disconnection.");      
  }

  return 0;
}

/*
 * See the documentation of `DPS_ID_SCOPE_t` in AzureIoT.h for details.
 */
static int mqtt_client_subscribe_function(mqtt_client_handle_t mqtt_client_handle, az_span topic, mqtt_qos_t qos)
{
  LogInfo("is mqtt client connected? %u", mqttClient.connected());
  LogInfo("MQTT client subscribing to '%.*s'", az_span_size(topic), az_span_ptr(topic));
   
  int result;
  MqttClient* mqttClientHandle = (MqttClient*)mqtt_client_handle;

// 2022-06-08 13:35:09 [INFO] MQTT client subscribing to '$dps/registrations/res/#'
// 2022-06-08 13:35:10 [ERROR] Failed subscribing to Azure Device Provisioning respose topic.
// issue with az_span_ptr again? still not working with string literal??

  int expiration = 0;
  int rc;
  // ArduinoMqttClient: 1 on success, 0 on failure
  rc = mqttClientHandle->subscribe((const char*)az_span_ptr(topic), (uint8_t)qos);
  LogInfo("is mqtt client connected? %u", mqttClient.connected());
  if (rc == 1)
  {
      LogInfo("MQTT topic subscribed");

      int packet_id = 0; // packet id unnaccessible in ArduinoMqttClient library (private).
      result = azure_iot_mqtt_client_subscribe_completed(&azure_iot, packet_id);
      if (result != RESULT_OK)
      {
        LogError("azure_iot_mqtt_client_subscribe_completed failed.");
      }
  }
  else
  {
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
  MqttClient* mqttClientHandle = (MqttClient*)mqtt_client_handle;

  int expiration = 0;
  int rc;
  // ArduinoMqttClient: 1 on success, 0 on failure
  rc = mqttClientHandle->beginMessage((const char*)az_span_ptr(mqtt_message->topic), 
                                      MQTT_DO_NOT_RETAIN_MSG, 
                                      (uint8_t)mqtt_message->qos);

  // ArduinoMqttClient: 1 on success, 0 on failure
  if (rc == 1) 
  {
    LogInfo("is mqtt client connected? %u", mqttClient.connected());
    mqttClientHandle->print((const char*)az_span_ptr(mqtt_message->payload));

    rc = mqttClientHandle->endMessage();
    if ( rc == 1)
    {
      int packet_id = 0; // packet id unnaccessible in ArduinoMqttClient library (private).
      result = azure_iot_mqtt_client_publish_completed(&azure_iot, packet_id);
    }
    else
    {
      Serial.println("message did not end");
      result = RESULT_ERROR;
    }
  }
  else
  {
    Serial.println("message did not begin");
     result = RESULT_ERROR;
  }
 
  return result;
}

/* --- Other Interface functions required by Azure IoT --- */

/*
 * See the documentation of `hmac_sha256_encryption_function_t` in AzureIoT.h for details.
 */
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

/*
 * See the documentation of `base64_decode_function_t` in AzureIoT.h for details.
 */
static int base64_decode(uint8_t* data, size_t data_length, uint8_t* decoded, size_t decoded_size, size_t* decoded_length)
{
  return mbedtls_base64_decode(decoded, decoded_size, decoded_length, data, data_length);
}

/*
 * See the documentation of `base64_encode_function_t` in AzureIoT.h for details.
 */
static int base64_encode(uint8_t* data, size_t data_length, uint8_t* encoded, size_t encoded_size, size_t* encoded_length)
{
  return mbedtls_base64_encode(encoded, encoded_size, encoded_length, data, data_length);
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

/* --- Arduino setup and loop Functions --- */
void setup()
{
  while (!Serial);
  Serial.begin(MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
  set_logging_function(logging_function);

  connect_to_wifi();
  ArduinoBearSSL.onGetTime(get_time);

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
  azure_iot_config.data_manipulation_functions.hmac_sha256_encrypt = mbedtls_hmac_sha256;
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
        LogInfo("Sending device info or telemetry");
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
    LogInfo("is mqtt client connected? %u", mqttClient.connected());
    LogInfo("polling");
    mqttClient.poll();
    ntpClient.update();
    delay(500);
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
static void connect_to_wifi()
{
  LogInfo("Connecting to WIFI wifi_ssid %s", IOT_CONFIG_WIFI_SSID);

  while (WiFi.begin(IOT_CONFIG_WIFI_SSID, IOT_CONFIG_WIFI_PASSWORD) != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  LogInfo("WiFi connected, IP address: %u", WiFi.localIP());
  LogInfo("Syncing time.");

  ntpClient.begin();
  while (!ntpClient.forceUpdate()) 
  {
    Serial.print(".");
  }
  Serial.println();

  LogInfo("Time synced!");
}

void onMessageReceived(int messageSize) 
{
  LogInfo("MQTT message received.");



  Serial.println(mqttClient.messageTopic().c_str());
  Serial.println(mqttClient.messageTopic().length());
  mqtt_message_t mqtt_message;
  mqtt_message.topic = az_span_create((uint8_t*)mqttClient.messageTopic().c_str(), mqttClient.messageTopic().length());
  Serial.println("hello world");

  mqttClient.read(message_buffer, (size_t)messageSize);
  Serial.print("message: ");
  Serial.println((char*)message_buffer);
  
  mqtt_message.payload = az_span_create(message_buffer, messageSize);
  mqtt_message.qos = mqtt_qos_at_most_once; // QoS is unused by azure_iot_mqtt_client_message_received. 


  if (azure_iot_mqtt_client_message_received(&azure_iot, &mqtt_message) != 0)
  {
    LogError("azure_iot_mqtt_client_message_received failed (topic=%s).", mqttClient.messageTopic().c_str());
  }
}

static String mqttErrorCodeName(int errorCode) 
{
  String errorMessage;
  switch (errorCode) 
  {
  case MQTT_CONNECTION_REFUSED:
    errorMessage = "MQTT_CONNECTION_REFUSED";
    break;
  case MQTT_CONNECTION_TIMEOUT:
    errorMessage = "MQTT_CONNECTION_TIMEOUT";
    break;
  case MQTT_SUCCESS:
    errorMessage = "MQTT_SUCCESS";
    break;
  case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
    errorMessage = "MQTT_UNACCEPTABLE_PROTOCOL_VERSION";
    break;
  case MQTT_IDENTIFIER_REJECTED:
    errorMessage = "MQTT_IDENTIFIER_REJECTED";
    break;
  case MQTT_SERVER_UNAVAILABLE:
    errorMessage = "MQTT_SERVER_UNAVAILABLE";
    break;
  case MQTT_BAD_USER_NAME_OR_PASSWORD:
    errorMessage = "MQTT_BAD_USER_NAME_OR_PASSWORD";
    break;
  case MQTT_NOT_AUTHORIZED:
    errorMessage = "MQTT_NOT_AUTHORIZED";
    break;
  default:
    errorMessage = "Unknown";
    break;
  }

  return errorMessage;
}

static uint32_t get_time()
{
    return (uint32_t)ntpClient.getUTCEpochTime();
}

static String getFormattedDateTime(uint32_t epochTimeInSeconds) 
{
  char buffer[256];

  time_t time = (time_t)epochTimeInSeconds;
  struct tm* timeInfo = localtime(&time);

  strftime(buffer, 20, "%F %T", timeInfo);

  return String(buffer);
}

static void logging_function(log_level_t log_level, char const* const format, ...)
{
  Serial.print(getFormattedDateTime(get_time() + GMT_OFFSET_SECS));

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
