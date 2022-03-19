// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for Arduino Portenta H7 boards.
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
 * - Initialize the MQTT client (here we use Arduino's ArduinoMqttClient, which also handle the tcp connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h` file.
 */

// C99 libraries
#include <cstdlib>
#include <cstdarg>
#include <string.h>
#include <time.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <NTPClient_Generic.h>
#include <ArduinoMqttClient.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c/" AZ_SDK_VERSION_STRING "(ard;ststm32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVER "pool.ntp.org"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

// Translate iot_configs.h defines into variables used by the sample
static const char *ssid = IOT_CONFIG_WIFI_SSID;
static const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static const char *mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char *device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;

static WiFiClient wifiClient;
static MqttClient mqttClient(wifiClient);

static WiFiUDP ntpUDP;
static NTPClient timeClient(ntpUDP, NTP_SERVER, GMT_OFFSET_SECS_DST);

static AzIoTSasToken sasToken(
  &client,
  AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
  AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
  AZ_SPAN_FROM_BUFFER(mqtt_password));

void onMqttMessage(int length) {

  Serial.print("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', duplicate = ");
  Serial.print(mqttClient.messageDup() ? "true" : "false");
  Serial.print(", QoS = ");
  Serial.print(mqttClient.messageQoS());
  Serial.print(", retained = ");
  Serial.print(mqttClient.messageRetain() ? "true" : "false");
  Serial.print("', length ");
  Serial.print(length);
  Serial.println(" bytes:");

  while (mqttClient.available()) {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();
}

static char *getTelemetryPayload() {
  az_span temp_span = az_span_create(telemetry_payload, sizeof(telemetry_payload));
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
  (void)az_span_u32toa(temp_span, telemetry_send_count++, &temp_span);
  temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
  temp_span = az_span_copy_u8(temp_span, '\0');

  return (char *)telemetry_payload;
}

static void sendTelemetry() {
  Logger.Info("Sending telemetry . . . ");
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
        &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL))) {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }
  char *telemetry = getTelemetryPayload();
  mqttClient.beginMessage(telemetry_topic, sizeof(telemetry), DO_NOT_RETAIN_MSG, MQTT_QOS1, false);
  mqttClient.print(telemetry);
  mqttClient.endMessage();
  Logger.Info("OK");
  delay(100);
}

static void initializeIoTHubClient() {
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
        &client,
        az_span_create((uint8_t *)host, strlen(host)),
        az_span_create((uint8_t *)device_id, strlen(device_id)),
        &options))) {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
        &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length))) {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
        &client, mqtt_username, sizeofarray(mqtt_username), NULL))) {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient() {
  int result;

  int token_result = sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES);
  if (token_result != 0) {
    Logger.Error("Failed generating SAS token");
    result = 1;
  }

  Logger.Info("MQTT client target uri set to " + String(mqtt_broker_uri));
  if (!mqttClient.connect(mqtt_broker_uri, mqtt_port)) {
    Logger.Error("Could not start mqtt client; error code:" + String(mqttClient.connectError()));
    mqttClient.stop();
    result = 1;
  } else {
    Logger.Info("MQTT client started");
    mqttClient.setId(mqtt_client_id);
    mqttClient.setKeepAliveInterval(30);
    mqttClient.setCleanSession(false);
    mqttClient.onMessage(onMqttMessage);

    Logger.Info("Subscribing to topic:" + String(telemetry_topic));
    mqttClient.subscribe(telemetry_topic, MQTT_QOS1);
    result = 0;
  }

  return result;
}

static void connectToWiFi() {
  Logger.Info("Connecting to WIFI SSID " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(3000);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + String(WiFi.localIP()));
}

static void initializeTime() {
  Logger.Info("Setting time using SNTP");

  timeClient.begin();

  Logger.Info("Time initialized!");
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}

  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  initializeMqttClient();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  if (sasToken.IsExpired() || !mqttClient.connected()) {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    initializeMqttClient();
  }

  mqttClient.poll();

  timeClient.update();

  if (millis() > next_telemetry_send_time_ms) {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }
}
