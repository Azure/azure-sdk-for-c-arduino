// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// C99 libraries
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Libraries for SSL client, MQTT client, WiFi connection, and SAS-token generation.
#include <ArduinoBearSSL.h>
#include <ArduinoMqttClient.h>
#include <WiFiNINA.h>
#include <mbed.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>

// Sample header
#include "iot_configs.h"

#define BUFFER_LENGTH_MQTT_TOPIC 128
#define BUFFER_LENGTH_MQTT_PASSWORD 256
#define BUFFER_LENGTH_MQTT_CLIENT_ID 256
#define BUFFER_LENGTH_MQTT_USERNAME 512
#define BUFFER_LENGTH_SAS 32
#define BUFFER_LENGTH_SAS_SIGNATURE 512
#define BUFFER_LENGTH_SAS_ENCODED_SIGNED_SIGNATURE 64
#define BUFFER_LENGTH_TIME 256

#define LED_PIN 2 // High on error. Briefly high for each successful send.
#define SECS_PER_MIN 60

// Logging
enum LogLevel 
{ 
  LogLevelDebug, 
  LogLevelInfo, 
  LogLevelError 
};

static String logString; // To construct logging String message.
static void log(LogLevel logLevel, String message);
#define LogDebug(message) log(LogLevelDebug, message)
#define LogInfo(message) log(LogLevelInfo, message)
#define LogError(message) log(LogLevelError, message)

// Sample static variables
static WiFiClient wiFiClient;
static BearSSLClient bearSSLClient(wiFiClient);
static MqttClient mqttClient(bearSSLClient);
static az_iot_hub_client azIoTHubClient;

static char mqttClientId[BUFFER_LENGTH_MQTT_CLIENT_ID];
static char mqttUsername[BUFFER_LENGTH_MQTT_USERNAME];
static char mqttPassword[BUFFER_LENGTH_MQTT_PASSWORD];
static char telemetryTopic[BUFFER_LENGTH_MQTT_TOPIC];
static unsigned long telemetryNextSendTimeMs;
static String telemetryPayload;
static uint32_t telemetrySendCount;

// Functions
void initializeAzureIoTClient();
void initializeMQTTClient();
void connectToWiFi();
void connectTMQTTClientToAzureIoTHub();

void onMessageReceived(int messageSize);
static void sendTelemetry();
static char* generateTelemetry();

static void generateMQTTPassword();
static void generateSASBase64EncodedSignedSignature(
    uint8_t const* sasSignature, size_t const sasSignatureSize,
    uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize,
    size_t* encodedSignedSignatureLength);
static uint64_t getSASTokenExpirationTime(uint32_t minutes);
static String getFormattedDateTime(unsigned long epochTimeInSeconds);
static unsigned long getTime();
static String mqttErrorCodeName(int errorCode);

void setup() 
{
  while (!Serial);
  Serial.begin(MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, HIGH);

  connectToWiFi();
  initializeAzureIoTHubClient();
  initializeMQTTClient();
  connectTMQTTClientToAzureIoTHub();

  digitalWrite(LED_PIN, LOW);
}

void loop() 
{
  if (WiFi.status() != WL_CONNECTED) 
  {
    connectToWiFi();
  }

  // Telemetry
  if (millis() > telemetryNextSendTimeMs) 
  {
    // Check for MQTT Client connection to Azure IoT hub. Reconnect if needed.
    if (!mqttClient.connected()) 
    {
      connectTMQTTClientToAzureIoTHub();
    }

    sendTelemetry();
    telemetryNextSendTimeMs = millis() + IOT_CONFIG_TELEMETRY_FREQUENCY_MS;
  }

  // MQTT loop must be called to process Telemetry and Cloud-to-Device (C2D) messages.
  mqttClient.poll();
  delay(500);
}

void connectToWiFi() 
{
  logString = "Attempting to connect to WIFI SSID: ";
  LogInfo(logString + IOT_CONFIG_WIFI_SSID);

  while (WiFi.begin(IOT_CONFIG_WIFI_SSID, IOT_CONFIG_WIFI_PASSWORD) != WL_CONNECTED) 
  {
    Serial.println(".");
    delay(IOT_CONFIG_WIFI_CONNECT_RETRY_MS);
  }
  Serial.println();

  logString = "WiFi connected, IP address: ";
  LogInfo(logString + WiFi.localIP() + ", Strength (dBm): " + WiFi.RSSI());
  LogInfo("Syncing time.");

  while (getTime() == 0) 
  {
    Serial.print(".");
  }
  Serial.println();

  LogInfo("Time synced!");
}

void initializeAzureIoTHubClient() 
{
  LogInfo("Initializing Azure IoT Hub client.");

  az_span hostname = AZ_SPAN_FROM_STR(IOT_CONFIG_IOTHUB_FQDN);
  az_span deviceId = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID);

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT);

  int rc = az_iot_hub_client_init(&azIoTHubClient, hostname, deviceId, &options);
  if (az_result_failed(rc)) 
  {
    logString = "Failed to initialize Azure IoT Hub client. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }

  logString = "Azure IoT Hub hostname: ";
  LogInfo( logString + IOT_CONFIG_IOTHUB_FQDN);
  LogInfo("Azure IoT Hub client initialized.");
}

void initializeMQTTClient() 
{

  LogInfo("Initializing MQTT client.");
  
  int rc;

  rc = az_iot_hub_client_get_client_id(&azIoTHubClient, mqttClientId, sizeof(mqttClientId), NULL);
  if (az_result_failed(rc)) 
  {
    logString = "Failed to get MQTT client ID. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }
  
  rc = az_iot_hub_client_get_user_name(&azIoTHubClient, mqttUsername, sizeof(mqttUsername), NULL);
  if (az_result_failed(rc)) 
  {
    logString = "Failed to get MQTT username. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }

  generateMQTTPassword(); // SAS Token

  mqttClient.setId(mqttClientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);
  mqttClient.onMessage(onMessageReceived); // Set callback for C2D messages

  logString = "MQTT Client ID: ";
  LogInfo(logString + mqttClientId);
  logString = "MQTT Username: ";
  LogInfo(logString + mqttUsername);
  logString = "MQTT Password (SAS Token): ";
  LogInfo("***");

  LogInfo("MQTT client initialized.");
}

void connectTMQTTClientToAzureIoTHub() 
{
  LogInfo("Connecting to Azure IoT Hub.");

  // Set a callback to get the current time used to validate the server certificate.
  ArduinoBearSSL.onGetTime(getTime);

  while (!mqttClient.connect(IOT_CONFIG_IOTHUB_FQDN, AZ_IOT_DEFAULT_MQTT_CONNECT_PORT)) 
  {
    int code = mqttClient.connectError();
    logString = "Cannot connect to Azure IoT Hub. Reason: ";
    LogError(logString + mqttErrorCodeName(code) + ", Code: " + code);
    delay(5000);
  }

  LogInfo("Connected to your Azure IoT Hub!");

  mqttClient.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  logString = "Subscribed to MQTT topic: ";
  LogInfo(logString + AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);
}

void onMessageReceived(int messageSize) 
{
  logString = "Message received: Topic: ";
  LogInfo(logString + mqttClient.messageTopic() + ", Length: " + messageSize);

  while (mqttClient.available()) 
  {
    LogInfo("Message: " + mqttClient.read());
  }
}

static void sendTelemetry() 
{
  digitalWrite(LED_PIN, HIGH);
  LogInfo("Arduino Nano RP2040 Connect sending telemetry . . . ");

  int rc = az_iot_hub_client_telemetry_get_publish_topic(&azIoTHubClient, NULL, telemetryTopic, 
                                                     sizeof(telemetryTopic), NULL);
  if (az_result_failed(rc)) 
  {
    logString = "Failed to get telemetry publish topic. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }

  mqttClient.beginMessage(telemetryTopic);
  mqttClient.print(generateTelemetry());
  mqttClient.endMessage();

  LogInfo("Telemetry sent.");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

static char* generateTelemetry() 
{
  String payloadStart = "{ \"msgCount\": ";
  telemetryPayload =  payloadStart + telemetrySendCount + " }";
  telemetrySendCount++;

  return (char*)telemetryPayload.c_str();
}

static void generateMQTTPassword() 
{
  int rc;

  uint64_t sasTokenDuration = 0;
  uint8_t signature[BUFFER_LENGTH_SAS_SIGNATURE] = {0};
  az_span signatureAzSpan = AZ_SPAN_FROM_BUFFER(signature);
  uint8_t encodedSignedSignature[BUFFER_LENGTH_SAS_ENCODED_SIGNED_SIGNATURE] = {0};
  size_t encodedSignedSignatureLength = 0;

  // Get the signature. It will be signed later with the decoded device key.
  // To change the sas token duration, see IOT_CONFIG_SAS_TOKEN_EXPIRY_MINUTES in iot_configs.h
  sasTokenDuration = getSASTokenExpirationTime(IOT_CONFIG_SAS_TOKEN_EXPIRY_MINUTES);
  rc = az_iot_hub_client_sas_get_signature(&azIoTHubClient, sasTokenDuration, 
                                           signatureAzSpan, &signatureAzSpan);
  if (az_result_failed(rc)) 
  {
    logString = "Could not get the signature for SAS Token. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }

  // Generate the encoded, signed signature (b64 encoded, HMAC-SHA256 signing).
  // Uses the decoded device key.
  generateSASBase64EncodedSignedSignature(
      az_span_ptr(signatureAzSpan), az_span_size(signatureAzSpan),
      encodedSignedSignature, sizeof(encodedSignedSignature), &encodedSignedSignatureLength);

  // Get the resulting MQTT password (SAS Token) from the base64 encoded, HMAC signed bytes.
  az_span encodedSignedSignatureAzSpan = az_span_create(encodedSignedSignature, 
                                                        encodedSignedSignatureLength);
  rc = az_iot_hub_client_sas_get_password(
      &azIoTHubClient, sasTokenDuration, encodedSignedSignatureAzSpan, AZ_SPAN_EMPTY,
      mqttPassword, sizeof(mqttPassword), NULL);
  if (az_result_failed(rc)) 
  {
    logString = "Could not get the MQTT password. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }
}

static void generateSASBase64EncodedSignedSignature(
    uint8_t const* sasSignature, size_t const sasSignatureSize,
    uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize,
    size_t* encodedSignedSignatureLength) 
{
  int rc;
  unsigned char sasDecodedKey[32] = {0};
  size_t sasDecodedKeyLength = 0;
  unsigned char sasHMAC256SignedSignature[32] = {0};
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  // Decode the SAS base64 encoded device key to use for HMAC signing.
  rc = mbedtls_base64_decode(sasDecodedKey, sizeof(sasDecodedKey),
                             &sasDecodedKeyLength,
                             (const unsigned char*)IOT_CONFIG_DEVICE_KEY,
                             sizeof(IOT_CONFIG_DEVICE_KEY) - 1);  // Do not include the ending '\0'
  if (rc != 0) 
  {
    logString = "mbedtls_base64_decode failed. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }

  // HMAC-SHA256 sign the signature with the decoded device key.
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, sasDecodedKey, sasDecodedKeyLength);
  mbedtls_md_hmac_update(&ctx, sasSignature, sasSignatureSize);
  mbedtls_md_hmac_finish(&ctx, sasHMAC256SignedSignature);
  mbedtls_md_free(&ctx);

  // Base64 encode the result of the HMAC signing.
  rc = mbedtls_base64_encode(encodedSignedSignature, encodedSignedSignatureSize,
                             encodedSignedSignatureLength,
                             sasHMAC256SignedSignature,
                             sizeof(sasHMAC256SignedSignature));
  if (rc != 0) 
  {
    logString = "mbedtls_base64_encode failed. Return code: ";
    LogError(logString + rc);
    exit(rc);
  }
}

static uint64_t getSASTokenExpirationTime(uint32_t minutes) 
{
  unsigned long now = getTime();
  unsigned long expiryTime = now + (SECS_PER_MIN* minutes);

  logString = "Current time: ";
  LogInfo(logString + getFormattedDateTime(now) + " (epoch: " + now + " secs)");
  logString = "Expiry time: ";
  LogInfo(logString + getFormattedDateTime(expiryTime) + " (epoch: " + expiryTime + " secs)");

  return (uint64_t)expiryTime;
}

static String getFormattedDateTime(unsigned long epochTimeInSeconds) 
{
  char buffer[BUFFER_LENGTH_TIME];

  time_t time = (time_t)epochTimeInSeconds;
  struct tm* timeInfo = localtime(&time);

  strftime(buffer, 20, "%FT%T", timeInfo);

  return String(buffer);
}

static unsigned long getTime()
{
    return WiFi.getTime();
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

static void log(LogLevel logLevel, String message) 
{
  Serial.print(getFormattedDateTime(getTime()));

  switch (logLevel) {
  case LogLevelDebug:
    Serial.print(" [DEBUG] ");
    break;
  case LogLevelInfo:
    Serial.print(" [INFO] ");
    break;
  case LogLevelError:
    Serial.print(" [ERROR] ");
    break;
  default:
    Serial.print(" [UNKNOWN] ");
    break;
  }

  Serial.print(message);
  Serial.println();
}
