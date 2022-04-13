// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

// C99 libraries
#include <Arduino.h>
#include <string.h>
#include <stdbool.h>
#include <cstdlib>
#include <time.h>

// Libraries for NTP, MQTT client, WiFi connection and SAS-token generation.
#include <mbed.h>
// #include <NTPClient.h>
#include <NTPClient_Generic.h>
#include <TimeLib.h>
// #include <sntp/sntp.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <utility/ECCX08SelfSignedCert.h>
#include <ArduinoMqttClient.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "iot_configs.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c/" AZ_SDK_VERSION_STRING "(ard;portentaH7)"

// Utility macros and defines
// Status LED: will remain high on error and pulled high for a short time for each successful send.
#define LED_PIN 2
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define MQTT_PACKET_SIZE 1024

/* --- Logging --- */
typedef enum log_level_t_enum
{
    log_level_debug,
    log_level_info,
    log_level_error
} log_level_t;
static void logging_function(log_level_t log_level, const char *format, ...);
#define Log(level, message, ...) logging_function(level, message, ##__VA_ARGS__)
#define LogInfo(message, ...) Log(log_level_info, message, ##__VA_ARGS__)
#define LogError(message, ...) Log(log_level_error, message, ##__VA_ARGS__)
#define LogDebug(message, ...) Log(log_level_debug, message, ##__VA_ARGS__)

/* --- Time and Time Zone --- */
// #define SECS_PER_MIN 60
// #define SECS_PER_HOUR 3600
#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * SECS_PER_HOUR)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * SECS_PER_HOUR)

// Translate arduino_secrets.h defines into variables used by the sample
const char *ssid = IOT_CONFIG_WIFI_SSID;
const char *password = IOT_CONFIG_WIFI_PASSWORD;
static const char *host = IOT_CONFIG_IOTHUB_FQDN;
static int wifi_connect_retry_interval = 10000; // milliseconds
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;
static int mqtt_retry_interval = 5000; // milliseconds

// Memory allocated for the sample's variables and structures.
static WiFiUDP ntp_udp_client;
static NTPClient timeClient(ntp_udp_client, "pool.ntp.org", GMT_OFFSET_SECS_DST);
static WiFiClient wifi_client;
static BearSSLClient ssl_client(wifi_client);
static MqttClient mqtt_client(ssl_client);
static az_iot_hub_client hub_client;

static char sas_token[200];
static size_t sas_token_length;
static uint8_t signature[512];

static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;
unsigned long sas_duration;

// Auxiliary functions
extern "C"
{
    extern int mbedtls_base64_decode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen);
    extern int mbedtls_base64_encode(unsigned char *dst, size_t dlen, size_t *olen, const unsigned char *src, size_t slen);
}

/* Function declarations */
static void mbedtls_hmac_sha256(az_span key, az_span payload, az_span signed_payload);
static void hmac_sha256_sign_signature(az_span decoded_key, az_span signature, az_span signed_signature, az_span *out_signed_signature);
static void base64_encode_bytes(az_span decoded_bytes, az_span base64_encoded_bytes, az_span *out_base64_encoded_bytes);
static void decode_base64_bytes(az_span base64_encoded_bytes, az_span decoded_bytes, az_span *out_decoded_bytes);
static void generate_sas_base64_encoded_signed_signature(az_span sas_base64_encoded_key, az_span sas_signature, az_span sas_base64_encoded_signed_signature, az_span *out_sas_base64_encoded_signed_signature);
static void generate_sas_key();
static int connect_to_azure_iot_hub();
static char *get_telemetry_payload();
static void send_telemetry();
void connectToWiFi();
void initializeClients();
void onMessageReceived(int messageSize);
void createCert();
unsigned long getTokenExpirationTime(uint32_t minutes);
const char* getLocaleDateTime(long epochTimeInSeconds);
unsigned long getTime();
const char* mqttErrorCodeName(int errorCode);

// Arduino setup and loop main functions.

void setup()
{
    while (!Serial)
        ;
    Serial.begin(MBED_CONF_PLATFORM_DEFAULT_SERIAL_BAUD_RATE);
    createCert();
    connectToWiFi();
    initializeClients();
    generate_sas_key();
    connect_to_azure_iot_hub();
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWiFi();
    }

    if (millis() > next_telemetry_send_time_ms)
    {
        // Check if connected, reconnect if needed.
        if (!mqtt_client.connected())
        {
            // Check if token is expired
            if (sas_duration <= getTime())
            {
                generate_sas_key();
            }
            connect_to_azure_iot_hub();
        }
        send_telemetry();
        next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
    }

    // MQTT loop must be called to process Device-to-Cloud and Cloud-to-Device.
    mqtt_client.poll();
    timeClient.update();
    delay(500);
}

void connectToWiFi()
{
    LogInfo("Attempting to connect to WIFI SSID: %s", ssid);

    while (WiFi.begin(ssid, password) != WL_CONNECTED)
    {
        Serial.println(".");
        delay(wifi_connect_retry_interval);
    }

    Serial.print("WiFi connected, IP address: ");

    Serial.print(WiFi.localIP());
    Serial.print(", Strength (dBm): ");
    Serial.println(WiFi.RSSI());

    Serial.print("Syncing time");
    timeClient.begin();
    timeClient.forceUpdate();

    while (!timeClient.updated())
    {
        Serial.print(".");
    }
    Serial.println();
    LogInfo("Time synced!");
}

void initializeClients()
{
    Serial.println("Initializing MQTT client");

    az_iot_hub_client_options options = az_iot_hub_client_options_default();
    options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

    if (az_result_failed(az_iot_hub_client_init(
            &hub_client,
            az_span_create((uint8_t *)host, strlen(host)),
            AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_ID),
            &options)))
    {
        Serial.println("Failed initializing Azure IoT Hub client");
        return;
    }

    Serial.println("MQTT client initialized");
}

static int connect_to_azure_iot_hub()
{
    size_t client_id_length;
    char mqtt_client_id[128];
    if (az_result_failed(az_iot_hub_client_get_client_id(
            &hub_client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
    {
        LogError("Failed to get MQTT client id, return code = %d", 1);
        return 1;
    }

    char mqtt_username[128];
    // Get the MQTT user name used to connect to IoT Hub
    if (az_result_failed(az_iot_hub_client_get_user_name(
            &hub_client, mqtt_username, sizeofarray(mqtt_username), NULL)))
    {
        LogError("Failed to get MQTT username, return code = %d", 1);
        return 1;
    }

    LogInfo("connect_to_azure_iot_hub - Broker: %s", host);
    LogInfo("connect_to_azure_iot_hub - Client ID: %s", mqtt_client_id);
    LogInfo("connect_to_azure_iot_hub - Username: %s", mqtt_username);
    LogInfo("connect_to_azure_iot_hub - SAS Token: %s", sas_token);

    mqtt_client.setId(mqtt_client_id);
    mqtt_client.setUsernamePassword(mqtt_username, sas_token);
    mqtt_client.onMessage(onMessageReceived);

    while (!mqtt_client.connect(host, mqtt_port))
    {
        // failed, retry
        int code = mqtt_client.connectError();
        String label = mqttErrorCodeName(code);
        LogError("Cannot connect to broker. Reason: %s, code=%d", label, code);
        delay(5000);
    }

    LogInfo("You're connected to the MQTT broker");

    mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

    return 0;
}

static char *get_telemetry_payload()
{
    az_span temp_span = az_span_create(telemetry_payload, sizeof(telemetry_payload));
    temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
    (void)az_span_u32toa(temp_span, telemetry_send_count++, &temp_span);
    temp_span = az_span_copy(temp_span, AZ_SPAN_FROM_STR(" }"));
    temp_span = az_span_copy_u8(temp_span, '\0');

    return (char *)telemetry_payload;
}

static void send_telemetry()
{
    digitalWrite(LED_PIN, HIGH);
    LogInfo("Arduino Nano Connect RP2040 sending telemetry . . . ");
    if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
            &hub_client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
    {
        Serial.println("Failed az_iot_hub_client_telemetry_get_publish_topic");
        return;
    }
    mqtt_client.beginMessage(telemetry_topic);
    mqtt_client.print(get_telemetry_payload());
    mqtt_client.endMessage();
    Serial.println("OK");
    delay(100);
    digitalWrite(LED_PIN, LOW);
}

static void generate_sas_key()
{
    az_result rc;
    // Create the POSIX expiration time from input minutes.
    sas_duration = getTokenExpirationTime(SAS_TOKEN_EXPIRY_IN_MINUTES);

    // Get the signature that will later be signed with the decoded key.
    az_span sas_signature = AZ_SPAN_FROM_BUFFER(signature);
    rc = az_iot_hub_client_sas_get_signature(&hub_client, (uint64_t)sas_duration, sas_signature, &sas_signature);
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

void onMessageReceived(int messageSize)
{
    // we received a message, print out the topic and contents
    LogInfo("Message received: topic=%s, length=%d", mqtt_client.messageTopic(), messageSize);
    // use the Stream interface to print the contents
    while (mqtt_client.available())
    {
        LogInfo("Message: %s", (char)mqtt_client.read());
    }
}

void createCert()
{
    if (!ECCX08.begin())
    {
        LogError("No ECCX08 present!");
        while (1)
            ;
    }

    // reconstruct the self signed cert
    ECCX08SelfSignedCert.beginReconstruction(0, 8);
    ECCX08SelfSignedCert.setCommonName(ECCX08.serialNumber());
    ECCX08SelfSignedCert.endReconstruction();

    // Set a callback to get the current time
    // used to validate the servers certificate
    ArduinoBearSSL.onGetTime(getTime);

    // Set the ECCX08 slot to use for the private key
    // and the accompanying public certificate for it
    ssl_client.setEccSlot(0, ECCX08SelfSignedCert.bytes(), ECCX08SelfSignedCert.length());
}

unsigned long getTokenExpirationTime(uint32_t minutes)
{
    long now = getTime();
    long expiryTime = now + (SECS_PER_MIN * minutes);
    LogInfo("Current time: %ls (epoch: %d secs)", getLocaleDateTime(now), now);
    LogInfo("Expiry time: %s (epoch: %d secs)", getLocaleDateTime(expiryTime), expiryTime);
    return expiryTime;
}

const char* getLocaleDateTime(long epochTimeInSeconds)
{
    char buf[32];
    time_t t = timeClient.getEpochTime();

    memset(buf, 0, sizeof(buf));
    sprintf(buf, "%d-%02d-%02dT%02d:%02d:%02d", year(t), month(t), day(t), hour(t), minute(t), second(t));
    return String(buf).c_str();
}

/* Get UTC time in seconds since January 1, 1970 */
unsigned long getTime()
{
    return timeClient.getUTCEpochTime();
}

const char* mqttErrorCodeName(int errorCode)
{
    String errMsg;
    switch (errorCode)
    {
    case MQTT_CONNECTION_REFUSED:
        errMsg = "MQTT_CONNECTION_REFUSED";
        break;
    case MQTT_CONNECTION_TIMEOUT:
        errMsg = "MQTT_CONNECTION_TIMEOUT";
        break;
    case MQTT_SUCCESS:
        errMsg = "MQTT_SUCCESS";
        break;
    case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
        errMsg = "MQTT_UNACCEPTABLE_PROTOCOL_VERSION";
        break;
    case MQTT_IDENTIFIER_REJECTED:
        errMsg = "MQTT_IDENTIFIER_REJECTED";
        break;
    case MQTT_SERVER_UNAVAILABLE:
        errMsg = "MQTT_SERVER_UNAVAILABLE";
        break;
    case MQTT_BAD_USER_NAME_OR_PASSWORD:
        errMsg = "MQTT_BAD_USER_NAME_OR_PASSWORD";
        break;
    case MQTT_NOT_AUTHORIZED:
        errMsg = "MQTT_NOT_AUTHORIZED";
        break;
    default:
        errMsg = "Unknown";
        break;
    }
    return errMsg.c_str();
}

static void logging_function(log_level_t log_level, const char *format, ...)
{
    Serial.print(getLocaleDateTime(getTime()));

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

    va_list ap;
    va_start(ap, format);
    
    char temp[256];
    char *buffer = temp;

    size_t len = vsnprintf(temp, sizeof(temp), format, ap);
    if (len > sizeof(temp) - 1)
    {
        buffer = new (std::nothrow) char[len + 1];
        if (!buffer)
        {
            return;
        }
        vsnprintf(buffer, len + 1, format, ap);
    }
    Serial.println(buffer);

    if (buffer != temp)
    {
        delete[] buffer;
    }
    va_end(ap);
}
