// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// C99 libraries
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Libraries for MQTT client, WiFi connection and SAS-token generation.
#include <ArduinoMqttClient.h>
#include <mbed.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <WiFiNINA.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>

// Sample header
#include "iot_configs.h"

#define BUFFER_LENGTH 128
#define BUFFER_LENGTH_SAS 256
#define BUFFER_LENGTH_SIGNATURE 512
#define BUFFER_LENGTH_CLIENT_ID 256
#define BUFFER_LENGTH_USERNAME 512

#define LED_PIN 2 // High on error. Briefly high for each successful send.
#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof(a[0]))

//#define SECS_PER_MIN 60
//#define SECS_PER_HOUR 3600
//#define GMT_OFFSET_SECS (IOT_CONFIG_TIME_ZONE * SECS_PER_HOUR)
//#define GMT_OFFSET_SECS_DST ((IOT_CONFIG_TIME_ZONE + IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR)

// Logging
typedef enum LogLevel
{
  LogLevelDebug,
  LogLevelInfo,
  LogLevelError
};

static void log(LogLevel logLevel, String message);
#define LogDebug(message) log(LogLevelDebug, message)
#define LogInfo(message) log(LogLevelInfo, message)
#define LogError(message) log(LogLevelError, message)

static WiFiClient wifiClient;
static BearSSLClient bearSSLClient(wifiClient);
static az_iot_hub_client azIoTHubClient;
static MqttClient mqttClient(bearSSLClient);


static char telemetryTopic[BUFFER_LENGTH];
static unsigned long nextTelemetrySendTimeMs = 0;
static uint32_t telemetrySendCount = 0;

// SAS Token generation
static char SASToken[BUFFER_LENGTH_SAS];
static size_t SASTokenLength;
static uint8_t signature[BUFFER_LENGTH_SIGNATURE];

// Functions
void establishConnection();
void connectToWiFi();
void generateSASToken();
void initializeAzureIoTClient();
void initializeMQTTClient();
void connectToAzureIoTHub();
void onMessageReceived(int messageSize);

static char* getTelemetryPayload();
static void sendTelemetry();
static String getFormattedDateTime(unsigned long epochTimeInSeconds);
static String mqttErrorCodeName(int errorCode);

void setup()
{
  while (!Serial);
  Serial.begin(MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  establishConnection();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }

  if (millis() > nextTelemetrySendTimeMs)
  {
    // Check if connected, reconnect if needed.
    if (!mqttClient.connected())
    {
      establishConnection();
    }
    sendTelemetry();
    nextTelemetrySendTimeMs = millis() + IOT_CONFIG_TELEMETRY_FREQUENCY_MS;
  }

  // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
  mqttClient.poll();
  delay(500);
}

void establishConnection()
{
  connectToWiFi();

  generateSASToken();

  initializeAzureIoTHubClient();

  initializeMQTTClient();

  connectToAzureIoTHub();

  digitalWrite(LED_PIN, LOW);
}

void connectToWiFi()
{
  LogInfo("Attempting to connect to WIFI SSID: %s", IOT_CONFIG_WIFI_SSID);

  while (WiFi.begin(IOT_CONFIG_WIFI_SSID, IOT_CONFIG_WIFI_PASSWORD) != WL_CONNECTED)
  {
    Serial.println(".");
    delay(IOT_CONFIG_WIFI_CONNECT_RETRY_MS);
  }

  LogInfo("WiFi connected, IP address: " + WiFi.localIP() + ", Strength (dBm): " + WiFi.RSSI());
  
  Serial.print("Syncing time");
  while(WiFi.getTime() == 0) {
    Serial.print(".");
  }
  Serial.println();

  LogInfo("Time synced!");
}

void initializeAzureIoTHubClient()
{
  LogInfo("Initializing Azure IoT Hub client.");

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT);

  int rc = az_iot_hub_client_init(&azIoTHubClient,
                                  az_span_create((uint8_t *)IOT_CONFIG_IOTHUB_FQDN, strlen(IOT_CONFIG_IOTHUB_FQDN)),
                                  AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID),
                                  &options);
  if (az_result_failed(rc))
  {
    LogError("Failed to initialize Azure IoT Hub client: az_result return code: " + rc + ".");
    exit(rc);
  }

  LogInfo("Azure IoT Hub client initialized.");
}

void initializeMQTTClient()
{
  LogInfo("Initializing MQTT client.");

  size_t clientIdLength;
  char mqttClientId[BUFFER_LENGTH_CLIENT_ID];
  int rc = az_iot_hub_client_get_client_id(&azIoTHubClient, 
                                          mqttClientId, 
                                          sizeof(clientIdLength) - 1, 
                                          &clientIdLength);
  if (az_result_failed(rc))
  {
    LogError("Failed to get MQTT client ID: az_result return code: " + rc ".");
    exit(rc);
  }

  char mqttUsername[BUFFER_LENGTH_USERNAME];
  rc = az_iot_hub_client_get_user_name(&azIoTHubClient, 
                                       mqttUsername, 
                                       SIZE_OF_ARRAY(mqttUsername), 
                                       NULL);
  if (az_result_failed(rc))
  {
    LogError("Failed to get MQTT username: az_result return code: " + rc ".");
    exit(rc);
  }

  mqttClient.setId(mqttClientId);
  mqttClient.setUsernamePassword(mqttUsername, SASToken);
  mqttClient.onMessage(onMessageReceived);

  LogInfo("Azure IoT Hub hostname: " + IOT_CONFIG_IOTHUB_FQDN);
  LogInfo("MQTT Client ID: " + mqttClientId);
  LogInfo("MQTT Username: " + mqttUsername);
  LogInfo("SAS Token: " + SASToken)
  
  LogInfo("MQTT client initialized.");
}

static int connectToAzureIoTHub()
{
  LogInfo("Connecting to Azure IoT Hub.");

  while (!mqttClient.connect(IOT_CONFIG_IOTHUB_FQDN, AZ_IOT_DEFAULT_MQTT_CONNECT_PORT))
  {
    int code = mqttClient.connectError();
    String label = mqttErrorCodeName(code);
    LogError("Cannot connect to Azure IoT Hub. Reason: " + label + ", code: " + code);
    delay(5000);
  }

  LogInfo("Connected to your Azure IoT Hub!");

  mqttClient.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  LogInfo("Subscribed to MQTT topic: " + AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  return 0;
}

static void sendTelemetry()
{
  digitalWrite(LED_PIN, HIGH);
  Serial.print(millis());
  LogInfo("Arduino Nano RP2040 Connect sending telemetry . . . ");

  int rc = az_iot_hub_client_telemetry_get_publish_topic(&azIoTHubClient, NULL, telemetryTopic, SIZE_OF_ARRAY(telemetryTopic), NULL);

  if (az_result_failed(rc))
  {
    LogError("Failed to get telemetry publish topic: az_result return code: " + rc ".");
    return;
  }

  mqttClient.beginMessage(telemetryTopic);
  mqttClient.print(getTelemetryPayload());
  mqttClient.endMessage();

  LogInfo("Telemetry sent.");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

static char* getTelemetryPayload()
{
  String telemetryPayload = "{ \"msgCount\": " + telemetrySendCount + " }";
  telemetrySendCount++;

  return telemetryPayload.c_str();
}






// Need to fix
void onMessageReceived(int messageSize)
{
  LogInfo("Message received: topic=" + mqttClient.messageTopic() + ", length=" + messageSize);

  while (mqttClient.available())
  {
    LogInfo("Message: " + mqttClient.read());
  }
}

static String getFormattedDateTime(unsigned long epochTimeInSeconds)
{
  char buffer[BUFFER_LENGTH];

  time_t time = (time_t)epochTimeInSeconds;
  struct tm *timeInfo = localtime(&time);

  strftime(buffer, 20, "%FT%T", timeInfo);

  return String(buffer);
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

static void log(log_level_t log_level, char const *const format, ...)
{
  Serial.print(getFormattedDateTime(WiFi.getTime()));

  switch (log_level)
  {
  case log_level_debug:
    Serial.print(" [DEBUG] ");
    break;
  case log_level_error:
    Serial.print(" [ERROR] ");
    break;
  default:
    Serial.print(" [INFO] ");
    break;
  }

  char message[256];
  va_list ap;
  va_start(ap, format);
  int message_length = vsnprintf(message, sizeof(message), format, ap);
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

int64_t getTokenExpirationTime(uint32_t minutes)
{
  long now = WiFi.getTime()();
  long expiryTime = now + (SECS_PER_MIN * minutes);
  LogInfo("Current time: %s (epoch: %d secs)", getFormattedDateTime(now), now);
  LogInfo("Expiry time: %s (epoch: %d secs)", getFormattedDateTime(expiryTime), expiryTime);
  return (int64_t)expiryTime;
}

static void generateSASToken()
{
  int rc;

  // Create the POSIX expiration time from input minutes.
  uint64_t sas_duration = getTokenExpirationTime(SAS_TOKEN_EXPIRY_IN_MINUTES);

  // Get the signature that will later be signed with the decoded key.
  az_span sas_signature = AZ_SPAN_FROM_BUFFER(signature);
  rc = az_iot_hub_client_sas_get_signature(&hub_client, sas_duration, sas_signature, &sas_signature);
  if (az_result_failed(rc))
  {
    LogError("Could not get the signature for SAS key: az_result return code = %d", rc);
  }

  // Generate the encoded, signed signature (b64 encoded, HMAC-SHA256 signing).
  char b64enc_hmacsha256_signature[64];
  az_span sas_base64_encoded_signed_signature = AZ_SPAN_FROM_BUFFER(b64enc_hmacsha256_signature);
  generate_sas_base64_encoded_signed_signature(
      AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
      sas_signature,
      sas_base64_encoded_signed_signature,
      &sas_base64_encoded_signed_signature);

  // Get the resulting MQTT password, passing the base64 encoded, HMAC signed bytes.
  size_t mqtt_password_length;
  rc = az_iot_hub_client_sas_get_password(
      &hub_client,
      sas_duration,
      sas_base64_encoded_signed_signature,
      AZ_SPAN_EMPTY,
      sas_token,
      sizeof(sas_token),
      &sas_token_length);
  if (az_result_failed(rc))
  {
    LogError("Could not get the password: az_result return code = %d", rc);
  }
}

static void mbedtls_hmac_sha256(az_span key, az_span payload, az_span signed_payload)
{
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)az_span_ptr(key), az_span_size(key));
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)az_span_ptr(payload), az_span_size(payload));
  mbedtls_md_hmac_finish(&ctx, (byte *)az_span_ptr(signed_payload));
  mbedtls_md_free(&ctx);
}

static void hmac_sha256_sign_signature(az_span decoded_key, az_span signature, az_span signed_signature, az_span *out_signed_signature)
{
  mbedtls_hmac_sha256(decoded_key, signature, signed_signature);
  *out_signed_signature = az_span_slice(signed_signature, 0, 32);
}

static void base64_encode_bytes(az_span decoded_bytes, az_span base64_encoded_bytes, az_span *out_base64_encoded_bytes)
{
  size_t len;
  if (mbedtls_base64_encode(az_span_ptr(base64_encoded_bytes), (size_t)az_span_size(base64_encoded_bytes),
                            &len, az_span_ptr(decoded_bytes), (size_t)az_span_size(decoded_bytes)) != 0)
  {
    Serial.println("[ERROR] mbedtls_base64_encode fail");
  }

  *out_base64_encoded_bytes = az_span_create(az_span_ptr(base64_encoded_bytes), (int32_t)len);
}

static void decode_base64_bytes(az_span base64_encoded_bytes, az_span decoded_bytes, az_span *out_decoded_bytes)
{

  memset(az_span_ptr(decoded_bytes), 0, (size_t)az_span_size(decoded_bytes));

  size_t len;
  if (mbedtls_base64_decode(az_span_ptr(decoded_bytes), (size_t)az_span_size(decoded_bytes),
                            &len, az_span_ptr(base64_encoded_bytes), (size_t)az_span_size(base64_encoded_bytes)) != 0)
  {
    Serial.println("[ERROR] mbedtls_base64_decode fail");
  }

  *out_decoded_bytes = az_span_create(az_span_ptr(decoded_bytes), (int32_t)len);
}

static void generate_sas_base64_encoded_signed_signature(az_span sas_base64_encoded_key, az_span sas_signature, az_span sas_base64_encoded_signed_signature, az_span *out_sas_base64_encoded_signed_signature)
{
  // Decode the sas base64 encoded key to use for HMAC signing.
  char sas_decoded_key_buffer[32];
  az_span sas_decoded_key = AZ_SPAN_FROM_BUFFER(sas_decoded_key_buffer);
  decode_base64_bytes(sas_base64_encoded_key, sas_decoded_key, &sas_decoded_key);

  // HMAC-SHA256 sign the signature with the decoded key.
  char sas_hmac256_signed_signature_buffer[32];
  az_span sas_hmac256_signed_signature = AZ_SPAN_FROM_BUFFER(sas_hmac256_signed_signature_buffer);
  hmac_sha256_sign_signature(sas_decoded_key, sas_signature, sas_hmac256_signed_signature, &sas_hmac256_signed_signature);

  // Base64 encode the result of the HMAC signing.
  base64_encode_bytes(
      sas_hmac256_signed_signature,
      sas_base64_encoded_signed_signature,
      out_sas_base64_encoded_signed_signature);
}
