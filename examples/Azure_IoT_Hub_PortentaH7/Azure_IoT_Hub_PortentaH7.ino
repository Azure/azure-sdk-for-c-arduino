// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*--- Libraries ---*/
// C99 libraries.
#include <cstdbool>
#include <cstdlib>
#include <cstring>
#include <time.h>

// Libraries for SSL client, MQTT client, NTP, and WiFi connection.
#include <ArduinoBearSSL.h>
#include <ArduinoMqttClient.h>
#include <NTPClient_Generic.h>
#include <TimeLib.h>
#include <WiFi.h>

// Libraries for SAS token generation.
#include <ECCX08.h>

// Azure IoT SDK for C includes.
#include <az_core.h>
#include <az_iot.h>

// Sample header.
#include "iot_configs.h"

// Logging
#include "SerialLogger.h"

/*--- Macros ---*/
#define BUFFER_LENGTH_MQTT_CLIENT_ID 256
#define BUFFER_LENGTH_MQTT_PASSWORD 256
#define BUFFER_LENGTH_MQTT_TOPIC 128
#define BUFFER_LENGTH_MQTT_USERNAME 512
#define BUFFER_LENGTH_SAS 32
#define BUFFER_LENGTH_SAS_ENCODED_SIGNED_SIGNATURE 64
#define BUFFER_LENGTH_SAS_SIGNATURE 512
#define BUFFER_LENGTH_DATETIME_STRING 256

#define LED_PIN 2 // High on error. Briefly high for each successful send.

// Time and Time Zone for NTP.
#define GMT_OFFSET_SECS (IOT_CONFIG_DAYLIGHT_SAVINGS ? \
                        ((IOT_CONFIG_TIME_ZONE + IOT_CONFIG_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR) : \
                        (IOT_CONFIG_TIME_ZONE * SECS_PER_HOUR))

// Exit into infinite loop
#define EXIT_LOOP(condition, errorMessage) \
  do \ 
  { \
    if (condition) { \
      Logger.Error(errorMessage); \
      while (1); \
    } \
  } while (0)

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
void initializeAzureIoTHubClient();
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
  Serial.begin(SERIAL_LOGGER_BAUD_RATE);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, HIGH);

  connectToWiFi();
  initializeAzureIoTHubClient();
  initializeMQTTClient();
  connectMQTTClientToAzureIoTHub();

  digitalWrite(LED_PIN, LOW);
  telemetryNextSendTimeMs = 0;
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
  delay(50);
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
  Logger.Info("Attempting to connect to WIFI SSID: " + String(IOT_CONFIG_WIFI_SSID));

  WiFi.begin(IOT_CONFIG_WIFI_SSID, IOT_CONFIG_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) 
  {
    Serial.println(".");
    delay(IOT_CONFIG_WIFI_CONNECT_RETRY_MS);
  }
  Serial.println();

  Logger.Info("WiFi connected, IP address: " + String(WiFi.localIP()) + ", Strength (dBm): " + WiFi.RSSI());
  Logger.Info("Syncing time.");

  ntpClient.begin();
  while (!ntpClient.forceUpdate()) 
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  Logger.Info("Time synced!");
}

/*
 * initializeAzureIoTHubClient:
 * The Azure IoT SDK for C client uses the provided hostname, device id, and user agent.
 */
void initializeAzureIoTHubClient() 
{
  Logger.Info("Initializing Azure IoT Hub client.");

  az_span hostname = AZ_SPAN_FROM_STR(IOT_CONFIG_IOTHUB_FQDN);
  az_span deviceId = AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID);

  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(IOT_CONFIG_AZURE_SDK_CLIENT_USER_AGENT);

  int result = az_iot_hub_client_init(&azIoTHubClient, hostname, deviceId, &options);

  EXIT_LOOP(az_result_failed(result), "Failed to initialize Azure IoT Hub client. Return code: " + result);

  Logger.Info("Azure IoT Hub hostname: " + String(IOT_CONFIG_IOTHUB_FQDN));
  Logger.Info("Azure IoT Hub client initialized.");
}

/*
 * initializeMQTTClient:
 * The MQTT client uses the client id and username from the Azure IoT SDK for C client.
 * The MQTT client uses the generated password (the SAS token).
 */
void initializeMQTTClient() 
{

  Logger.Info("Initializing MQTT client.");
  
  int result;

  result = az_iot_hub_client_get_client_id(
      &azIoTHubClient, mqttClientId, sizeof(mqttClientId), NULL);
  EXIT_LOOP(az_result_failed(result), "Failed to get MQTT client ID. Return code: " + result);
  
  result = az_iot_hub_client_get_user_name(
      &azIoTHubClient, mqttUsername, sizeof(mqttUsername), NULL);
  EXIT_LOOP(az_result_failed(result), "Failed to get MQTT username. Return code: " + result);

  generateMQTTPassword(); // SAS Token

  mqttClient.setId(mqttClientId);
  mqttClient.setUsernamePassword(mqttUsername, mqttPassword);
  mqttClient.onMessage(onMessageReceived); // Set callback for C2D messages

  Logger.Info("Client ID: " + String(mqttClientId));
  Logger.Info("Username: " + String(mqttUsername));

  Logger.Info("MQTT client initialized.");
}

/*
 * connectMQTTClientToAzureIoTHub:
 * The SSL library sets a callback to validate the server certificate.
 * The MQTT client connects to the provided hostname. The port is pre-set.
 * The MQTT client subscribes to the Cloud to Device (C2D) topic to receive messages.
 */
void connectMQTTClientToAzureIoTHub() 
{
  Logger.Info("Connecting to Azure IoT Hub.");

  // Set a callback to get the current time used to validate the server certificate.
  ArduinoBearSSL.onGetTime(getTime);

  while (!mqttClient.connect(IOT_CONFIG_IOTHUB_FQDN, AZ_IOT_DEFAULT_MQTT_CONNECT_PORT)) 
  {
    int code = mqttClient.connectError();
    Logger.Error("Cannot connect to Azure IoT Hub. Reason: " + mqttErrorCodeName(code) + ", Code: " + code);
    delay(5000);
  }

  Logger.Info("Connected to your Azure IoT Hub!");

  mqttClient.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  Logger.Info("Subscribed to MQTT topic: " + String(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC));
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
  Logger.Info("Message received: Topic: " + mqttClient.messageTopic() + ", Length: " + messageSize);
  Logger.Info("Message: ");

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
  Logger.Info("Arduino Nano RP2040 Connect sending telemetry . . . ");

  int result = az_iot_hub_client_telemetry_get_publish_topic(
      &azIoTHubClient, NULL, telemetryTopic, sizeof(telemetryTopic), NULL);
  EXIT_LOOP(az_result_failed(result), "Failed to get telemetry publish topic. Return code: " + result);

  mqttClient.beginMessage(telemetryTopic);
  mqttClient.print(generateTelemetry());
  mqttClient.endMessage();

  Logger.Info("Telemetry sent.");
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
  telemetryPayload =  String("{ \"msgCount\": ") + telemetrySendCount + " }";
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
  EXIT_LOOP(az_result_failed(result), "Could not get the signature for SAS Token. Return code: " + result);

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
  EXIT_LOOP(az_result_failed(result), "Could not get the MQTT password. Return code: " + result);
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
  az_span sasDecodedKeySpan = AZ_SPAN_FROM_BUFFER(sasDecodedKey);
  int32_t sasDecodedKeyLength = 0;
  uint8_t sasHMAC256SignedSignature[BUFFER_LENGTH_SAS] = {0};

  // Decode the SAS base64 encoded device key to use for HMAC signing.
  az_span configDeviceKeySpan = az_span_create((uint8_t*)IOT_CONFIG_DEVICE_KEY, sizeof(IOT_CONFIG_DEVICE_KEY) - 1);
  result = az_base64_decode(sasDecodedKeySpan, configDeviceKeySpan, &sasDecodedKeyLength);
  EXIT_LOOP(result != AZ_OK, "az_base64_decode failed. Return code: " + result);

  // HMAC-SHA256 sign the signature with the decoded device key.
  result = ECCX08.begin();
  EXIT_LOOP(!result, "Failed to communicate with ATECC608.");
  
  result = ECCX08.nonce(sasDecodedKey);
  EXIT_LOOP(!result, "Failed to do nonce.");

  result = ECCX08.beginHMAC(0xFFFF);
  EXIT_LOOP(!result, "Failed to start HMAC operation.");

  result = ECCX08.updateHMAC(sasSignature, sasSignatureSize);
  EXIT_LOOP(!result, "Failed to update HMAC with signature.");

  result = ECCX08.endHMAC(sasHMAC256SignedSignature);
  EXIT_LOOP(!result, "Failed to end HMAC operation.");

  // Base64 encode the result of the HMAC signing.
  az_span signedSignatureSpan = az_span_create(sasHMAC256SignedSignature, sizeof(sasHMAC256SignedSignature));
  az_span encodedSignedSignatureSpan = az_span_create(encodedSignedSignature, encodedSignedSignatureSize);
  result = az_base64_encode(encodedSignedSignatureSpan, signedSignatureSpan, (int32_t*) encodedSignedSignatureLength);
  EXIT_LOOP(result != AZ_OK, "az_base64_encode failed. Return code: " + result);
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

  Logger.Info("UTC Current time: " + getFormattedDateTime(now) + " (epoch: " + now + " secs)");
  Logger.Info("UTC Expiry time: " + getFormattedDateTime(expiryTime) + " (epoch: " + expiryTime + " secs)");
  Logger.Info("Local Current time: " + getFormattedDateTime(localNow));
  Logger.Info("Local Expiry time: " + getFormattedDateTime(localExpiryTime));

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
  char dateTimeString[BUFFER_LENGTH_DATETIME_STRING];

  time_t epochTimeInSecondsAsTimeT = (time_t)epochTimeInSeconds;
  struct tm* timeInfo = localtime(&epochTimeInSecondsAsTimeT);

  strftime(dateTimeString, 20, "%F %T", timeInfo);

  return String(dateTimeString);
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
