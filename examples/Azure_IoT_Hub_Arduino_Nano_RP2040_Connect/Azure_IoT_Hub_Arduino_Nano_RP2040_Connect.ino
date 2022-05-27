// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// C99 libraries
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Libraries for MQTT client, WiFi connection, and SAS-token generation.
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

#define BUFFER_LENGTH 256
#define BUFFER_LENGTH_SIGNATURE 512
#define BUFFER_LENGTH_SIGNED_SIGNATURE 64
#define BUFFER_LENGTH_MQTT_TOPIC 128
#define BUFFER_LENGTH_MQTT_PASSWORD 256
#define BUFFER_LENGTH_MQTT_CLIENT_ID 256
#define BUFFER_LENGTH_MQTT_USERNAME 512

#define LED_PIN 2 // High on error. Briefly high for each successful send.
#define SIZE_OF_ARRAY(a) (sizeof(a) / sizeof(a[0]))
#define SECS_PER_MIN 60

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

// Sample static variables
static WiFiClient wiFiClient;
static BearSSLClient bearSSLClient(wiFiClient);
static az_iot_hub_client azIoTHubClient;
static MqttClient mqttClient(bearSSLClient);

static char mqttPassword[BUFFER_LENGTH_MQTT_PASSWORD];
static size_t mqttPasswordLength;
static char telemetryTopic[BUFFER_LENGTH_MQTT_TOPIC];
static unsigned long telemetryNextSendTimeMs = 0;
static uint32_t telemetrySendCount = 0;

// Functions
void establishConnection();
void connectToWiFi();
void initializeAzureIoTClient();
void initializeMQTTClient();
void connectToAzureIoTHub();

void onMessageReceived(int messageSize);
static void sendTelemetry();
static char* generateTelemetry();

static void getMQTTPassword();
static uint64_t getSASTokenExpirationTime(uint32_t minutes);
static void generateSASBase64EncodedSignedSignature(uint8_t const* sasSignature, size_t const sasSignatureSize, uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize, size_t* encodedSignedSignatureLength)
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

  if (millis() > telemetryNextSendTimeMs)
  {
    // Check if connected, reconnect if needed.
    if (!mqttClient.connected())
    {
      establishConnection();
    }

    sendTelemetry();
    telemetryNextSendTimeMs = millis() + IOT_CONFIG_TELEMETRY_FREQUENCY_MS;
  }

  // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
  mqttClient.poll();
  delay(500);
}

void establishConnection()
{
  connectToWiFi();
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
  Serial.println();

  LogInfo("WiFi connected, IP address: " + WiFi.localIP() + ", Strength (dBm): " + WiFi.RSSI());
  LogInfo("Syncing time.");

  while(WiFi.getTime() == 0) 
  {
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

  int rc = az_iot_hub_client_init(&azIoTHubClient, az_span_create((uint8_t *)IOT_CONFIG_IOTHUB_FQDN, strlen(IOT_CONFIG_IOTHUB_FQDN)),
                                  AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID), &options);
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
  char mqttClientId[BUFFER_LENGTH_MQTT_CLIENT_ID];
  int rc = az_iot_hub_client_get_client_id(&azIoTHubClient, mqttClientId, sizeof(clientIdLength) - 1, &clientIdLength);
  if (az_result_failed(rc))
  {
    LogError("Failed to get MQTT client ID: az_result return code: " + rc ".");
    exit(rc);
  }

  char mqttUsername[BUFFER_LENGTH_MQTT_USERNAME];
  rc = az_iot_hub_client_get_user_name(&azIoTHubClient, mqttUsername, SSIZE_OF_ARRAY(mqttUsername), NULL);
  if (az_result_failed(rc))
  {
    LogError("Failed to get MQTT username: az_result return code: " + rc ".");
    exit(rc);
  }

  generateMQTTPassword(); // SAS Token

  mqttClient.setId(mqttClientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);
  mqttClient.onMessage(onMessageReceived);

  LogInfo("Azure IoT Hub hostname: " + IOT_CONFIG_IOTHUB_FQDN);
  LogInfo("MQTT Client ID: " + mqttClientId);
  LogInfo("MQTT Username: " + mqttUsername);
  LogInfo("MQTT Password (SAS Token): " + mqttPassword)
  
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

void onMessageReceived(int messageSize)
{
  LogInfo("Message received: topic=" + mqttClient.messageTopic() + ", length=" + messageSize);

  while (mqttClient.available())
  {
    LogInfo("Message: " + mqttClient.read()); // Needs to be fixed?
  }
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
  mqttClient.print(generateTelemetry());
  mqttClient.endMessage();

  LogInfo("Telemetry sent.");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

static char* generateTelemetry()
{
  String telemetryPayload = "{ \"msgCount\": " + telemetrySendCount + " }";
  telemetrySendCount++;

  return telemetryPayload.c_str();
}

static void generateMQTTPassword()
{
  int rc;

  uint64_t sasTokenDuration = 0;
  uint8_t signature[BUFFER_LENGTH_SIGNATURE];
  az_span signatureAzSpan = AZ_SPAN_FROM_BUFFER(signature);
  uint8_t signedSignature[BUFFER_LENGTH_SIGNED_SIGNATURE] = { 0 };
  size_t signedSignatureLength = 0;
  
  // Get the signature. It will be signed later with the decoded device key.
  sasTokenDuration = getSASTokenExpirationTime(IOT_CONFIG_SAS_TOKEN_EXPIRY_MINUTES);
  rc = az_iot_hub_client_sas_get_signature(&azIoTHubClient, sasTokenDuration, signatureAzSpan, &signatureAzSpan);
  if (az_result_failed(rc))
  {
    LogError("Could not get the signature for SAS Token: az_result return code: " + rc);
    exit(rc);
  }

  // Generate the encoded, signed signature (b64 encoded, HMAC-SHA256 signing). Uses the decoded device key.
  generateSASBase64EncodedSignedSignature(az_span_ptr(signatureAzSpan), az_span_size(signatureAzSpan), signedSignature, sizeof(signedSignature), &signedSignatureLength);

  // Get the resulting MQTT password (SAS Token) from the base64 encoded, HMAC signed bytes.
  az_span signedSignatureAzSpan = az_span_create(signedSignature, (int32_t)signedSignatureLength);
  rc = az_iot_hub_client_sas_get_password(
      &azIoTHubClient,
      sasTokenDuration,
      signedSignatureAzSpan,
      AZ_SPAN_EMPTY,
      mqttPassword,
      sizeof(mqttPassword),
      &mqttPasswordLength);
  if (az_result_failed(rc))
  {
    LogError("Could not get the MQTT password: az_result return code: " + rc);
    exit(rc);
  }
}

static uint64_t getSASTokenExpirationTime(uint32_t minutes)
{
  unsigned long now = WiFi.getTime()();
  unsigned long expiryTime = now + (SECS_PER_MIN * minutes);

  LogInfo("Current time: %s (epoch: %d secs)", getFormattedDateTime(now), now);
  LogInfo("Expiry time: %s (epoch: %d secs)", getFormattedDateTime(expiryTime), expiryTime);

  return (uint64_t)expiryTime;
}

static void generateSASBase64EncodedSignedSignature(uint8_t const* sasSignature, size_t const sasSignatureSize, uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize, size_t* encodedSignedSignatureLength)
{
  int rc;
  unsigned char sasDecodedKey[32] = { 0 };
  size_t sasDecodedKeyLength = 0;
  unsigned char sasHMAC256SignedSignature[32] = { 0 };
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  // Decode the SAS base64 encoded device key to use for HMAC signing.
  rc = mbedtls_base64_decode(sasDecodedKey, sizeof(sasDecodedKey), &sasDecodedKeyLength, 
                              IOT_CONFIG_DEVICE_KEY, sizeof(IOT_CONFIG_DEVICE_KEY));
  if (rc != 0)
  {
    LogError("mbedtls_base64_decode failed. Return code: " + rc);
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
  rc = mbedtls_base64_encode(encodedSignedSignature, encodedSignedSignatureSize, encodedSignedSignatureLength, 
                            sasHMAC256SignedSignature, sizeof(sasHMAC256SignedSignature));
  if (rc != 0)
  {
    LogError("mbedtls_base64_encode failed. Return code: " + rc);
    exit(rc);
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

static void log(LogLevel logLevel, String message)
{
  Serial.print(getFormattedDateTime(WiFi.getTime()));

  switch (logLevel)
  {
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

