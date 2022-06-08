// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*--- Libraries ---*/
// C99 libraries.
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <ctime>

// Libraries for SSL client, MQTT client, NTP, and WiFi connection.
#include <ArduinoBearSSL.h>
#include <ArduinoMqttClient.h>
#include <NTPClient_Generic.h>
#include <TimeLib.h>
#include <WiFi.h>

// Libraries for SAS token generation.
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

// Azure IoT SDK for C includes.
#include <az_core.h>
#include <az_iot.h>

// Sample header.
#include "iot_configs.h"

/*--- Macros ---*/
#define BUFFER_LENGTH_MQTT_CLIENT_ID 256
#define BUFFER_LENGTH_MQTT_PASSWORD 256
#define BUFFER_LENGTH_MQTT_TOPIC 128
#define BUFFER_LENGTH_MQTT_USERNAME 512
#define BUFFER_LENGTH_SAS 32
#define BUFFER_LENGTH_SAS_ENCODED_SIGNED_SIGNATURE 64
#define BUFFER_LENGTH_SAS_SIGNATURE 512
#define BUFFER_LENGTH_TIME 256

#define LED_PIN 2 // High on error. Briefly high for each successful send.

// Time and Time Zone for NTP.
#define GMT_OFFSET_SECS (IOT_CONFIG_DAYLIGHT_SAVINGS ? \
                        ((IOT_CONFIG_TIME_ZONE + IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR) : \
                        (IOT_CONFIG_TIME_ZONE * SECS_PER_HOUR))
/*--- Logging ---*/
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

/*--- Sample static variables --*/
// Clients for NTP, WiFi connection, SSL, MQTT, and Azure IoT SDK for C.
static WiFiUDP wiFiUDPClient;
static NTPClient ntpClient(wiFiUDPClient);
static WiFiClient wiFiClient;
static BearSSLClient bearSSLClient(wiFiClient);
static MqttClient mqttClient(bearSSLClient);
static az_iot_hub_client azIoTHubClient;

// MQTT variables.
static char mqttClientId[BUFFER_LENGTH_MQTT_CLIENT_ID];
static char mqttUsername[BUFFER_LENGTH_MQTT_USERNAME];
static char mqttPassword[BUFFER_LENGTH_MQTT_PASSWORD];

// Telemetry variables.
static char telemetryTopic[BUFFER_LENGTH_MQTT_TOPIC];
static unsigned long telemetryNextSendTimeMs;
static String telemetryPayload;
static uint32_t telemetrySendCount;

/*--- Functions ---*/
// Initialization and connection functions.
void connectToWiFi();
void initializeAzureIoTClient();
void initializeMQTTClient();
void connectMQTTClientToAzureIoTHub();

// Telemetry and message-callback functions.
void onMessageReceived(int messageSize);
static void sendTelemetry();
static char* generateTelemetry();

// SAS Token related functions.
static void generateMQTTPassword();
static void generateSASBase64EncodedSignedSignature(
    uint8_t const* sasSignature, size_t const sasSignatureSize,
    uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize,
    size_t* encodedSignedSignatureLength);
static uint64_t getSASTokenExpirationTime(uint32_t minutes);

// Time and Error functions.
static unsigned long getTime();
static String getFormattedDateTime(unsigned long epochTimeInSeconds);
static String mqttErrorCodeName(int errorCode);

/*---------------------------*/
/*    Main code execution    */
/*---------------------------*/

/*
 * setup:
 * Initialization and connection of serial communication, WiFi client, Azure IoT SDK for C client, 
 * and MQTT client.
 */
void setup()
{
  while (!Serial);
  Serial.begin(MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, HIGH);

  connectToWiFi();
  initializeAzureIoTHubClient();
  initializeMQTTClient();
  connectMQTTClientToAzureIoTHub();

  digitalWrite(LED_PIN, LOW);
}

/*
 * loop:
 * Check for connection and reconnect if necessary.
 * Send Telemetry and receive messages.
 */
void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
      connectToWiFi();
  }

  // Telemetry
  if (millis() > telemetryNextSendTimeMs)
  {
    // Check for MQTT Client connection to Azure IoT Hub. Reconnect if needed.
    if (!mqttClient.connected())
    {
      connectMQTTClientToAzureIoTHub();
    }
    
    sendTelemetry();
    telemetryNextSendTimeMs = millis() + IOT_CONFIG_TELEMETRY_FREQUENCY_MS;
  }

  // MQTT loop must be called to process Telemetry and Cloud-to-Device (C2D) messages.
  mqttClient.poll();
  ntpClient.update();
  delay(500);
}

/*-----------------------------------------------*/
/*    Initialization and connection functions    */
/*-----------------------------------------------*/

/*
 * connectToWifi:
 * The WiFi client connects, using the provided SSID and password.
 * The NTP client synchronizes the time on the device. 
 */
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

  ntpClient.begin();
  while (!ntpClient.forceUpdate()) 
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  LogInfo("Time synced!");
}

/*
 * initializeAzureIoTHubClient:
 * The Azure IoT SDK for C client uses the provided hostname, device id, and user agent.
 */
void initializeAzureIoTHubClient() 
{
  LogInfo("Initializing Azure IoT Hub client.");

  az_span hostname = AZ_SPAN_FROM_STR(IOT_CONFIG_IOTHUB_FQDN);
  az_span deviceId = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID);

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT);

  int result = az_iot_hub_client_init(&azIoTHubClient, hostname, deviceId, &options);
  if (az_result_failed(result)) 
  {
    logString = "Failed to initialize Azure IoT Hub client. Return code: ";
    LogError(logString + result);
    exit(result);
  }

  logString = "Azure IoT Hub hostname: ";
  LogInfo( logString + IOT_CONFIG_IOTHUB_FQDN);
  LogInfo("Azure IoT Hub client initialized.");
}

/*
 * initializeMQTTClient:
 * The MQTT client uses the client id and username from the Azure IoT SDK for C client.
 * The MQTT client uses the generated password (the SAS token).
 */
void initializeMQTTClient() 
{

  LogInfo("Initializing MQTT client.");
  
  int result;

  result = az_iot_hub_client_get_client_id(
      &azIoTHubClient, mqttClientId, sizeof(mqttClientId), NULL);
  if (az_result_failed(result)) 
  {
    logString = "Failed to get MQTT client ID. Return code: ";
    LogError(logString + result);
    exit(result);
  }
  
  result = az_iot_hub_client_get_user_name(
      &azIoTHubClient, mqttUsername, sizeof(mqttUsername), NULL);
  if (az_result_failed(result)) 
  {
    logString = "Failed to get MQTT username. Return code: ";
    LogError(logString + result);
    exit(result);
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
  LogInfo(logString + "***");

  LogInfo("MQTT client initialized.");
}

/*
 * connectMQTTClientToAzureIoTHub:
 * The SSL library sets a callback to validate the server certificate.
 * The MQTT client connects to the provided hostname. The port is pre-set.
 * The MQTT client subscribes to the Cloud to Device (C2D) topic to receive messages.
 */
void connectMQTTClientToAzureIoTHub() 
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

/*------------------------------------------------*/
/*    Telemetry and message-callback functions    */
/*------------------------------------------------*/

/*
 * onMessageReceived:
 * The function called when device receives a message on the subscribed C2D topic.
 * Callback function signature is defined by the ArduinoMQTTClient library.
 * Message received is printed to the terminal.
 */
void onMessageReceived(int messageSize) 
{
  logString = "Message received: Topic: ";
  LogInfo(logString + mqttClient.messageTopic() + ", Length: " + messageSize);
  LogInfo("Message: ");

  while (mqttClient.available()) 
  {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();
}

/*
 * sendTelemetry:
 * The Azure IoT SDK for C client creates the MQTT topic to publish a telemetry message.
 * The MQTT client creates and sends the telemetry mesage on the topic.
 */
static void sendTelemetry()
{
  digitalWrite(LED_PIN, HIGH);
  LogInfo("Arduino Portenta H7 sending telemetry . . . ");

  int result = az_iot_hub_client_telemetry_get_publish_topic(
      &azIoTHubClient, NULL, telemetryTopic, sizeof(telemetryTopic), NULL);
  if (az_result_failed(result)) 
  {
    logString = "Failed to get telemetry publish topic. Return code: ";
    LogError(logString + result);
    exit(result);
  }

  mqttClient.beginMessage(telemetryTopic);
  mqttClient.print(generateTelemetry());
  mqttClient.endMessage();

  LogInfo("Telemetry sent.");
  delay(100);
  digitalWrite(LED_PIN, LOW);
}

/*
 * generateTelemetry:
 * Simulated telemetry.  
 * In your application, this function should retrieve real telemetry data from the device and format
 * it as needed.
 */
static char* generateTelemetry() 
{
  String payloadStart = "{ \"msgCount\": ";
  telemetryPayload =  payloadStart + telemetrySendCount + " }";
  telemetrySendCount++;

  return (char*)telemetryPayload.c_str();
}

/*************************************/
/*    SAS Token related functions    */
/*************************************/

/*
 * generateMQTTPassword:
 * The MQTT password is the generated SAS token. The process is:
 *    1. Get the SAS token expiration time from the provided value. (Default 60 minutes).
 *    2. Azure IoT SDK for C creates the SAS signature from this expiration time.
 *    3. Sign and encode the SAS signature.
 *    4. Azure IoT SDK for C creates the MQTT Password from the expiration time and the encoded,
 *       signed SAS signature.
 */
static void generateMQTTPassword() 
{
  int result;

  uint64_t sasTokenDuration = 0;
  uint8_t signature[BUFFER_LENGTH_SAS_SIGNATURE] = {0};
  az_span signatureAzSpan = AZ_SPAN_FROM_BUFFER(signature);
  uint8_t encodedSignedSignature[BUFFER_LENGTH_SAS_ENCODED_SIGNED_SIGNATURE] = {0};
  size_t encodedSignedSignatureLength = 0;

  // Get the signature. It will be signed later with the decoded device key.
  // To change the sas token duration, see IOT_CONFIG_SAS_TOKEN_EXPIRY_MINUTES in iot_configs.h
  sasTokenDuration = getSASTokenExpirationTime(IOT_CONFIG_SAS_TOKEN_EXPIRY_MINUTES);
  result = az_iot_hub_client_sas_get_signature(
      &azIoTHubClient, sasTokenDuration, signatureAzSpan, &signatureAzSpan);
  if (az_result_failed(result)) 
  {
    logString = "Could not get the signature for SAS Token. Return code: ";
    LogError(logString + result);
    exit(result);
  }

  // Sign and encode the signature (b64 encoded, HMAC-SHA256 signing).
  // Uses the decoded device key.
  generateSASBase64EncodedSignedSignature(
      az_span_ptr(signatureAzSpan), az_span_size(signatureAzSpan),
      encodedSignedSignature, sizeof(encodedSignedSignature), &encodedSignedSignatureLength);

  // Get the resulting MQTT password (SAS Token) from the base64 encoded, HMAC signed bytes.
  az_span encodedSignedSignatureAzSpan = az_span_create(encodedSignedSignature, 
                                                        encodedSignedSignatureLength);
  result = az_iot_hub_client_sas_get_password(
      &azIoTHubClient, sasTokenDuration, encodedSignedSignatureAzSpan, AZ_SPAN_EMPTY,
      mqttPassword, sizeof(mqttPassword), NULL);
  if (az_result_failed(result)) 
  {
    logString = "Could not get the MQTT password. Return code: ";
    LogError(logString + result);
    exit(result);
  }
}

/*
 * generateSASBase64EncodedSignedSignature:
 * Sign and encode a signature. It is signed using the provided device key.
 * The process is:
 *    1. Decode the encoded device key.
 *    2. Sign the signature with the decoded device key.
 *    3. Encode the signed signature.
 */
static void generateSASBase64EncodedSignedSignature(
    uint8_t const* sasSignature, size_t const sasSignatureSize,
    uint8_t* encodedSignedSignature, size_t encodedSignedSignatureSize,
    size_t* encodedSignedSignatureLength) 
{
  int result;
  unsigned char sasDecodedKey[BUFFER_LENGTH_SAS] = {0};
  size_t sasDecodedKeyLength = 0;
  unsigned char sasHMAC256SignedSignature[BUFFER_LENGTH_SAS] = {0};
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;

  // Decode the SAS base64 encoded device key to use for HMAC signing.
  result = mbedtls_base64_decode(
      sasDecodedKey, sizeof(sasDecodedKey), &sasDecodedKeyLength,
      (const unsigned char*)IOT_CONFIG_DEVICE_KEY, sizeof(IOT_CONFIG_DEVICE_KEY) - 1);
  if (result != 0) 
  {
    logString = "mbedtls_base64_decode failed. Return code: ";
    LogError(logString + result);
    exit(result);
  }

  // HMAC-SHA256 sign the signature with the decoded device key.
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, sasDecodedKey, sasDecodedKeyLength);
  mbedtls_md_hmac_update(&ctx, sasSignature, sasSignatureSize);
  mbedtls_md_hmac_finish(&ctx, sasHMAC256SignedSignature);
  mbedtls_md_free(&ctx);

  // Base64 encode the result of the HMAC signing.
  result = mbedtls_base64_encode(
      encodedSignedSignature, encodedSignedSignatureSize, encodedSignedSignatureLength,
      sasHMAC256SignedSignature, sizeof(sasHMAC256SignedSignature));
  if (result != 0) 
  {
    logString = "mbedtls_base64_encode failed. Return code: ";
    LogError(logString + result);
    exit(result);
  }
}

/*
 * getSASTokenExpirationTime:
 * Calculate expiration time from current time and duration value.
 */
static uint64_t getSASTokenExpirationTime(uint32_t minutes) 
{
  unsigned long now = getTime();  // GMT
  unsigned long expiryTime = now + (SECS_PER_MIN * minutes); // For SAS Token
  unsigned long localNow = now + GMT_OFFSET_SECS;
  unsigned long localExpiryTime = expiryTime + GMT_OFFSET_SECS;

  logString = "UTC Current time: ";
  LogInfo(logString + getFormattedDateTime(now) + " (epoch: " + now + " secs)");
  logString = "UTC Expiry time: ";
  LogInfo(logString + getFormattedDateTime(expiryTime) + " (epoch: " + expiryTime + " secs)");
  logString = "Local Current time: ";
  LogInfo(logString + getFormattedDateTime(localNow));
  logString = "Local Expiry time: ";
  LogInfo(logString + getFormattedDateTime(localExpiryTime));

  return (uint64_t)expiryTime;
}

/**********************************/
/*    Time and Error functions    */
/**********************************/

/*
 * getTime:
 * NTP client returns the seconds corresponding to GMT epoch time.
 * This function used as a callback by the SSL library to validate the server certificate
 * and in SAS token generation.
 */
static unsigned long getTime()
{
    return ntpClient.getUTCEpochTime();
}

/*
 * getFormattedDateTime:
 * Custom formatting for epoch seconds. Used in logging.
 */
static String getFormattedDateTime(unsigned long epochTimeInSeconds) 
{
  char buffer[BUFFER_LENGTH_TIME];

  time_t time = (time_t)epochTimeInSeconds;
  struct tm* timeInfo = localtime(&time);

  strftime(buffer, 20, "%F %T", timeInfo);

  return String(buffer);
}

/*
 * mqttErrorCodeName:
 * Legibly prints AruinoMqttClient library error enum values. 
 */
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

/**************************/
/*    Logging functions   */
/**************************/

/*
 * log:
 * Prints the time, log level, and log message.
 */
static void log(LogLevel logLevel, String message) 
{
  Serial.print(getFormattedDateTime(getTime() + GMT_OFFSET_SECS));

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
